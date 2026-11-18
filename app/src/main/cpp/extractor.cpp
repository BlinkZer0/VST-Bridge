// SPDX-License-Identifier: GPL-3.0-or-later
//
// Streaming tar.zst extractor for the Wine/Box64 payload.
//
// Supports ustar, GNU long names ('L'/'K') and pax ('x') headers; regular
// files, directories, symlinks and hard links. Restores permission bits and
// file mtimes (Wine compares timestamps of its builtin files).
//
// Relocation: the payload is built with a long placeholder prefix. On the
// device we rewrite it to the real install root in
//   - ELF PT_INTERP (the dynamic loader path of Box64),
//   - "#!" script files,
//   - symlink targets.
// The real root must not be longer than the placeholder.

#include "extractor.h"

#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zstd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace vg {
namespace {

std::string errnoStr(const std::string& what, const std::string& path) {
    return what + " " + path + ": " + std::strerror(errno);
}

// Reads the decompressed byte stream of a zstd file region.
class ZstdReader {
public:
    ZstdReader(int fd, int64_t offset, int64_t length)
        : fd_(fd), offset_(offset), length_(length),
          dctx_(ZSTD_createDCtx()),
          // Big reads: per-call cost through /sdcard (FUSE) is high. 128 KiB reads made a
          // 63 MB archive take 107 s from Android/data vs 1.8 s from the APK.
          inBuf_(4 << 20), outBuf_(1 << 20) {
        posix_fadvise(fd, offset, length, POSIX_FADV_SEQUENTIAL);
    }
    ~ZstdReader() { ZSTD_freeDCtx(dctx_); }

    bool read(void* dst, size_t n) {
        auto* d = static_cast<char*>(dst);
        while (n > 0) {
            if (outStart_ == outEnd_ && !fill()) return false;
            size_t k = std::min(n, outEnd_ - outStart_);
            std::memcpy(d, outBuf_.data() + outStart_, k);
            outStart_ += k; d += k; n -= k;
        }
        return true;
    }

    bool skip(size_t n) {
        while (n > 0) {
            if (outStart_ == outEnd_ && !fill()) return false;
            size_t k = std::min(n, outEnd_ - outStart_);
            outStart_ += k; n -= k;
        }
        return true;
    }

    int64_t consumed() const { return inPos_; }
    const std::string& error() const { return err_; }

private:
    bool fill() {
        outStart_ = outEnd_ = 0;
        for (;;) {
            if (in_.pos == in_.size && !inEof_) {
                int64_t want = std::min<int64_t>((int64_t)inBuf_.size(), length_ - inPos_);
                ssize_t got = want > 0 ? pread(fd_, inBuf_.data(), (size_t)want, offset_ + inPos_) : 0;
                if (got < 0) { err_ = std::string("read archive: ") + std::strerror(errno); return false; }
                if (got == 0) inEof_ = true;
                inPos_ += got;
                in_ = ZSTD_inBuffer{inBuf_.data(), (size_t)got, 0};
            }
            ZSTD_outBuffer out{outBuf_.data(), outBuf_.size(), 0};
            size_t ret = ZSTD_decompressStream(dctx_, &out, &in_);
            if (ZSTD_isError(ret)) { err_ = std::string("zstd: ") + ZSTD_getErrorName(ret); return false; }
            outEnd_ = out.pos;
            if (outEnd_ > 0) return true;
            if (inEof_ && in_.pos == in_.size) return false;  // clean end of stream
        }
    }

    int fd_;
    int64_t offset_, length_, inPos_ = 0;
    ZSTD_DCtx* dctx_;
    std::vector<char> inBuf_, outBuf_;
    ZSTD_inBuffer in_{nullptr, 0, 0};
    size_t outStart_ = 0, outEnd_ = 0;
    bool inEof_ = false;
    std::string err_;
};

uint64_t parseNumber(const char* f, size_t n) {
    if (static_cast<unsigned char>(f[0]) & 0x80) {  // GNU base-256
        uint64_t v = static_cast<unsigned char>(f[0]) & 0x7f;
        for (size_t i = 1; i < n; ++i) v = (v << 8) | static_cast<unsigned char>(f[i]);
        return v;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < n && f[i]; ++i) {
        if (f[i] == ' ') { if (v) break; else continue; }
        if (f[i] < '0' || f[i] > '7') break;
        v = (v << 3) | static_cast<uint64_t>(f[i] - '0');
    }
    return v;
}

std::string field(const char* f, size_t n) { return std::string(f, strnlen(f, n)); }

bool checksumOk(const char* h) {
    uint64_t want = parseNumber(h + 148, 8), sum = 0;
    for (int i = 0; i < 512; ++i)
        sum += (i >= 148 && i < 156) ? ' ' : static_cast<unsigned char>(h[i]);
    return sum == want;
}

// Rejects absolute paths and ".." so a bad archive cannot escape destDir.
bool sanitize(std::string& p) {
    while (p.rfind("./", 0) == 0) p.erase(0, 2);
    while (!p.empty() && p.back() == '/') p.pop_back();
    if (p.empty() || p[0] == '/') return false;
    size_t s = 0;
    while (s <= p.size()) {
        size_t e = p.find('/', s);
        if (e == std::string::npos) e = p.size();
        if (p.compare(s, e - s, "..") == 0 && e - s == 2) return false;
        s = e + 1;
    }
    return true;
}

bool mkdirs(const std::string& path, mode_t mode = 0755) {
    if (path.empty()) return true;
    struct stat st{};
    if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    size_t slash = path.rfind('/');
    if (slash != std::string::npos && slash > 0 && !mkdirs(path.substr(0, slash), mode)) return false;
    return mkdir(path.c_str(), mode) == 0 || errno == EEXIST;
}

std::string parentOf(const std::string& p) {
    size_t s = p.rfind('/');
    return s == std::string::npos ? std::string() : p.substr(0, s);
}

bool replacePrefix(std::string& s, const std::string& from, const std::string& to) {
    if (s.rfind(from, 0) != 0) return false;
    s = to + s.substr(from.size());
    return true;
}

// Rewrites PT_INTERP if it starts with the placeholder. Returns false on hard error.
bool fixInterp(int fd, const ExtractOptions& opt, ExtractStats& st, std::string& err, const std::string& path) {
    Elf64_Ehdr eh{};
    if (pread(fd, &eh, sizeof eh, 0) != (ssize_t)sizeof eh) return true;
    if (std::memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 || eh.e_ident[EI_CLASS] != ELFCLASS64) return true;
    for (int i = 0; i < eh.e_phnum; ++i) {
        Elf64_Phdr ph{};
        if (pread(fd, &ph, sizeof ph, (off_t)(eh.e_phoff + (uint64_t)i * eh.e_phentsize)) != (ssize_t)sizeof ph) return true;
        if (ph.p_type != PT_INTERP || ph.p_filesz == 0 || ph.p_filesz > 4096) continue;
        std::vector<char> buf(ph.p_filesz);
        if (pread(fd, buf.data(), buf.size(), (off_t)ph.p_offset) != (ssize_t)buf.size()) return true;
        std::string interp(buf.data(), strnlen(buf.data(), buf.size()));
        if (!replacePrefix(interp, opt.placeholder, opt.realRoot)) return true;
        if (interp.size() + 1 > buf.size()) { err = "PT_INTERP too long after relocation in " + path; return false; }
        std::fill(buf.begin(), buf.end(), 0);
        std::memcpy(buf.data(), interp.data(), interp.size());
        if (pwrite(fd, buf.data(), buf.size(), (off_t)ph.p_offset) != (ssize_t)buf.size()) {
            err = errnoStr("rewrite PT_INTERP", path); return false;
        }
        ++st.interpRewrites;
    }
    return true;
}

bool fixShebang(int fd, uint64_t size, const ExtractOptions& opt, ExtractStats& st, std::string& err, const std::string& path) {
    if (size < 2 || size > (4u << 20)) return true;
    std::string data(size, '\0');
    if (pread(fd, data.data(), size, 0) != (ssize_t)size) return true;
    if (data.compare(0, 2, "#!") != 0) return true;
    bool changed = false;
    for (size_t pos = 0; (pos = data.find(opt.placeholder, pos)) != std::string::npos; pos += opt.realRoot.size()) {
        data.replace(pos, opt.placeholder.size(), opt.realRoot);
        changed = true;
    }
    if (!changed) return true;
    if (ftruncate(fd, 0) != 0 || pwrite(fd, data.data(), data.size(), 0) != (ssize_t)data.size()) {
        err = errnoStr("rewrite script", path); return false;
    }
    ++st.shebangRewrites;
    return true;
}

bool copyFile(const std::string& from, const std::string& to, std::string& err) {
    int in = open(from.c_str(), O_RDONLY | O_CLOEXEC);
    if (in < 0) { err = errnoStr("open hardlink source", from); return false; }
    struct stat st{};
    fstat(in, &st);
    unlink(to.c_str());
    int out = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, st.st_mode & 07777);
    if (out < 0) { err = errnoStr("create", to); close(in); return false; }
    std::vector<char> buf(1 << 20);
    ssize_t n;
    bool ok = true;
    while ((n = read(in, buf.data(), buf.size())) > 0)
        if (write(out, buf.data(), (size_t)n) != n) { err = errnoStr("write", to); ok = false; break; }
    close(in);
    close(out);
    return ok;
}

}  // namespace

std::string extractTarZst(const ExtractOptions& opt, ExtractStats& st) {
    auto t0 = std::chrono::steady_clock::now();
    if (opt.realRoot.size() > opt.placeholder.size())
        return "install path (" + opt.realRoot + ") is longer than the payload placeholder (" +
               std::to_string(opt.placeholder.size()) + " chars)";
    if (!mkdirs(opt.destDir)) return errnoStr("mkdir", opt.destDir);

    ZstdReader r(opt.fd, opt.offset, opt.length);
    std::string longName, longLink, err;
    std::map<std::string, std::string> pax;
    std::vector<char> buf(1 << 20);
    char h[512];
    int zeroBlocks = 0;
    int64_t lastReport = 0;

    for (;;) {
        if (!r.read(h, sizeof h)) {
            if (!r.error().empty()) return r.error();
            break;  // EOF on a header boundary: treat as end of archive
        }
        bool allZero = true;
        for (char c : h) if (c) { allZero = false; break; }
        if (allZero) { if (++zeroBlocks >= 2) break; continue; }
        zeroBlocks = 0;
        if (!checksumOk(h)) return "corrupt tar header (checksum mismatch)";

        char type = h[156];
        uint64_t size = parseNumber(h + 124, 12);
        if (auto it = pax.find("size"); it != pax.end()) size = std::stoull(it->second);
        uint64_t padded = (size + 511) & ~uint64_t(511);

        // Metadata entries that apply to the next real entry.
        if (type == 'L' || type == 'K' || type == 'x') {
            std::string data(size, '\0');
            if (!r.read(data.data(), size) || !r.skip(padded - size)) return "truncated archive";
            if (type == 'L') longName = data.c_str();
            else if (type == 'K') longLink = data.c_str();
            else {
                size_t p = 0;
                while (p < data.size()) {
                    size_t sp = data.find(' ', p);
                    if (sp == std::string::npos) break;
                    size_t len = std::stoul(data.substr(p, sp - p));
                    if (len == 0 || p + len > data.size()) break;
                    std::string rec = data.substr(sp + 1, len - (sp - p) - 2);  // drop trailing '\n'
                    size_t eq = rec.find('=');
                    if (eq != std::string::npos) pax[rec.substr(0, eq)] = rec.substr(eq + 1);
                    p += len;
                }
            }
            continue;
        }
        if (type == 'g') { if (!r.skip(padded)) return "truncated archive"; continue; }

        std::string name;
        if (!longName.empty()) name = longName;
        else if (auto it = pax.find("path"); it != pax.end()) name = it->second;
        else {
            name = field(h, 100);
            std::string prefix = field(h + 345, 155);
            if (std::memcmp(h + 257, "ustar", 5) == 0 && !prefix.empty()) name = prefix + "/" + name;
        }
        std::string link;
        if (!longLink.empty()) link = longLink;
        else if (auto it = pax.find("linkpath"); it != pax.end()) link = it->second;
        else link = field(h + 157, 100);
        longName.clear(); longLink.clear(); pax.clear();

        mode_t mode = (mode_t)(parseNumber(h + 100, 8) & 0777);
        int64_t mtime = (int64_t)parseNumber(h + 136, 12);

        std::string rel = name;
        if (!sanitize(rel)) {
            if (rel.empty() && type == '5') { if (!r.skip(padded)) return "truncated archive"; continue; }  // "./"
            return "unsafe path in archive: " + name;
        }
        std::string path = opt.destDir + "/" + rel;
        if (!mkdirs(parentOf(path))) return errnoStr("mkdir", parentOf(path));

        switch (type) {
        case '5':
            if (!mkdirs(path, 0755)) return errnoStr("mkdir", path);
            chmod(path.c_str(), mode | 0700);
            ++st.dirs;
            break;
        case '2': {
            if (replacePrefix(link, opt.placeholder, opt.realRoot)) ++st.symlinkRewrites;
            unlink(path.c_str());
            if (symlink(link.c_str(), path.c_str()) != 0) return errnoStr("symlink", path);
            ++st.symlinks;
            break;
        }
        case '1': {
            std::string target = link;
            if (!sanitize(target)) return "unsafe hardlink target: " + link;
            if (!copyFile(opt.destDir + "/" + target, path, err)) return err;
            ++st.hardlinks;
            break;
        }
        case '0': case '\0': case '7': {
            unlink(path.c_str());
            int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
            if (fd < 0) return errnoStr("create", path);
            uint64_t left = size;
            while (left > 0) {
                size_t k = (size_t)std::min<uint64_t>(left, buf.size());
                if (!r.read(buf.data(), k)) { close(fd); return r.error().empty() ? "truncated archive" : r.error(); }
                if (write(fd, buf.data(), k) != (ssize_t)k) { std::string e = errnoStr("write", path); close(fd); return e; }
                left -= k;
            }
            if (!r.skip(padded - size)) { close(fd); return "truncated archive"; }
            bool ok = fixInterp(fd, opt, st, err, path) && fixShebang(fd, size, opt, st, err, path);
            fchmod(fd, mode);
            struct timespec ts[2] = {{mtime, 0}, {mtime, 0}};
            futimens(fd, ts);
            close(fd);
            if (!ok) return err;
            st.bytesWritten += size;
            ++st.files;
            padded = 0;  // data already consumed
            break;
        }
        default:
            break;  // device nodes, fifos: skip their (empty) data below
        }
        if (type != '0' && type != '\0' && type != '7' && !r.skip(padded)) return "truncated archive";

        if (opt.progress && r.consumed() - lastReport > (1 << 20)) {
            lastReport = r.consumed();
            if (!opt.progress(r.consumed(), opt.length)) return "cancelled";
        }
    }
    if (opt.progress) opt.progress(opt.length, opt.length);
    st.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return {};
}

}  // namespace vg
