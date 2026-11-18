// SPDX-License-Identifier: GPL-3.0-or-later
// See vg_wl.h. Wire format: each message is [object id][size << 16 | opcode][args...], 32-bit words in
// host byte order; strings/arrays are length-prefixed and padded to 4 bytes; fds travel as
// SCM_RIGHTS ancillary data in protocol order.
#include "vg_wl.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace vgwl {
namespace {

enum class If {
    display, registry, callback, compositor, surface, region, subcompositor, subsurface, shm, shm_pool,
    buffer, wm_base, positioner, xdg_surface, xdg_toplevel, xdg_popup, viewporter, viewport, seat, pointer,
    keyboard, touch, output, unknown
};

// Globals we advertise: registry name = index + 1.
struct Global { const char* name; If type; uint32_t version; };
const Global kGlobals[] = {
    {"wl_compositor", If::compositor, 4},
    {"wl_subcompositor", If::subcompositor, 1},
    {"wl_shm", If::shm, 1},
    {"xdg_wm_base", If::wm_base, 2},
    {"wp_viewporter", If::viewporter, 1},
    {"wl_seat", If::seat, 5},
    {"wl_output", If::output, 2},
};

constexpr uint32_t kFmtArgb = 0, kFmtXrgb = 1;
constexpr uint32_t kBtnLeft = 0x110;

uint32_t nowMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000);
}

int32_t toFixed(double v) { return (int32_t)std::lround(v * 256.0); }
double fromFixed(int32_t v) { return v / 256.0; }

struct Client;

struct Obj {
    If type = If::unknown;
    uint32_t id = 0, version = 1;
    Client* client = nullptr;
    virtual ~Obj() = default;
};
using ObjP = std::shared_ptr<Obj>;

struct Pool {
    void* mem = MAP_FAILED;
    size_t size = 0;
    int fd = -1;
    ~Pool() {
        if (mem != MAP_FAILED) munmap(mem, size);
        if (fd >= 0) close(fd);
    }
};

struct PoolObj : Obj { std::shared_ptr<Pool> pool; };

struct BufferObj : Obj {
    std::shared_ptr<Pool> pool;
    int32_t offset = 0, w = 0, h = 0, stride = 0;
    uint32_t format = 0;
};

struct Rect { int x, y, w, h; bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; } };
using RegionOps = std::vector<std::pair<bool, Rect>>;  // (add?, rect) in order

bool regionContains(const RegionOps& ops, int x, int y) {
    bool in = false;
    for (const auto& [add, r] : ops)
        if (r.contains(x, y)) in = add;
    return in;
}

struct RegionObj : Obj { RegionOps ops; };

struct SurfaceObj;
using SurfaceW = std::weak_ptr<SurfaceObj>;

struct ViewportState { bool src = false; double sx = 0, sy = 0, sw = 0, sh = 0; bool dst = false; int dw = 0, dh = 0; };

struct SurfaceObj : Obj {
    // pending (double-buffered) state
    bool pAttach = false;
    std::weak_ptr<BufferObj> pBuffer;  // empty + pAttach => attach(NULL)
    int pScale = 1;
    bool pInputSet = false, pInputInfinite = true;
    RegionOps pInput;
    std::vector<ObjP> pFrames;
    ViewportState pVp;
    // current state
    std::vector<uint32_t> px;
    int bw = 0, bh = 0;
    bool mapped = false, argb = false;
    int scale = 1;
    bool inputInfinite = true;
    RegionOps input;
    ViewportState vp;
    // role
    enum Role { None, Toplevel, Sub, Cursor } role = None;
    SurfaceW parent;
    int subX = 0, subY = 0;
    std::vector<SurfaceW> children;  // bottom -> top, all drawn above this surface
    std::weak_ptr<Obj> xdgSurface, xdgToplevel;
    bool configured = false;
    uint64_t mapSeq = 0;
    std::string title;

    int logicalW() const {
        if (vp.dst) return vp.dw;
        if (vp.src) return (int)vp.sw;
        return bw / std::max(scale, 1);
    }
    int logicalH() const {
        if (vp.dst) return vp.dh;
        if (vp.src) return (int)vp.sh;
        return bh / std::max(scale, 1);
    }
};

struct XdgSurfaceObj : Obj { SurfaceW surface; };
struct XdgToplevelObj : Obj { SurfaceW surface; std::weak_ptr<Obj> xdgSurface; };
struct SubsurfaceObj : Obj { SurfaceW surface; };
struct ViewportObj : Obj { SurfaceW surface; };

// Outgoing message builder.
struct Msg {
    std::vector<uint32_t> w;
    Msg(uint32_t id, uint16_t op) : w{id, op} {}
    Msg& u(uint32_t v) { w.push_back(v); return *this; }
    Msg& i(int32_t v) { w.push_back((uint32_t)v); return *this; }
    Msg& f(double v) { w.push_back((uint32_t)toFixed(v)); return *this; }
    Msg& s(const std::string& str) {
        const uint32_t len = (uint32_t)str.size() + 1;
        w.push_back(len);
        const size_t base = w.size();
        w.resize(base + (len + 3) / 4, 0);
        memcpy(&w[base], str.c_str(), len);
        return *this;
    }
    Msg& arr(const std::vector<uint32_t>& a) {
        w.push_back((uint32_t)(a.size() * 4));
        w.insert(w.end(), a.begin(), a.end());
        return *this;
    }
};

struct Client {
    int fd = -1;
    std::vector<uint8_t> in, out;
    std::deque<int> fds;
    std::map<uint32_t, ObjP> objs;
    std::vector<std::weak_ptr<Obj>> pointers;
    std::vector<std::weak_ptr<Obj>> outputs;
    bool dead = false;

    ~Client() {
        for (int f : fds) close(f);
        if (fd >= 0) close(fd);
    }
    void send(Msg& m) {
        const uint32_t op = m.w[1] & 0xFFFF;
        m.w[1] = (uint32_t)(m.w.size() * 4) << 16 | op;
        const uint8_t* b = reinterpret_cast<const uint8_t*>(m.w.data());
        out.insert(out.end(), b, b + m.w.size() * 4);
    }
    void deleteId(uint32_t id) {
        Msg m(1, 1);
        m.u(id);
        send(m);
    }
};

// Request argument reader.
struct Args {
    const uint32_t* p;
    size_t n, i = 0;
    Client* c;
    bool bad = false;
    uint32_t u() { if (i >= n) { bad = true; return 0; } return p[i++]; }
    int32_t in() { return (int32_t)u(); }
    std::string s() {
        uint32_t len = u();
        if (len == 0) return {};
        size_t words = (len + 3) / 4;
        if (i + words > n) { bad = true; return {}; }
        std::string r(reinterpret_cast<const char*>(p + i), len - 1);
        i += words;
        return r;
    }
    int fd() {
        if (c->fds.empty()) { bad = true; return -1; }
        int f = c->fds.front();
        c->fds.pop_front();
        return f;
    }
};

bool alive(const Obj* o) { return o && o->client && !o->client->dead; }

struct Placed { std::shared_ptr<SurfaceObj> s; int x, y, w, h; };

}  // namespace

struct Compositor::Impl {
    std::mutex m;
    std::thread th;
    std::atomic<bool> run{false};
    int listenFd = -1, wakeR = -1, wakeW = -1;
    std::string path;
    int outW = 1920, outH = 1080;
    std::vector<std::unique_ptr<Client>> clients;
    std::vector<SurfaceW> toplevels;  // in map order
    std::vector<std::weak_ptr<Obj>> readyFrames;
    uint32_t serial = 1;
    uint64_t mapCounter = 0;
    uint64_t sceneSerial = 1;
    // last composed canvas origin (for input mapping)
    int originX = 0, originY = 0;
    std::shared_ptr<SurfaceObj> shownTop;
    // pointer focus
    std::weak_ptr<SurfaceObj> focus;
    bool grabbed = false;
    uint64_t commits = 0, connects = 0, protoErrors = 0;

    // ------------------------------------------------------------ helpers
    void dirty() { ++sceneSerial; }

    template <class T>
    std::shared_ptr<T> get(Client* c, uint32_t id, If type) {
        auto it = c->objs.find(id);
        if (it == c->objs.end() || it->second->type != type) return nullptr;
        return std::static_pointer_cast<T>(it->second);
    }

    template <class T>
    std::shared_ptr<T> create(Client* c, uint32_t id, If type, uint32_t version) {
        auto o = std::make_shared<T>();
        o->type = type;
        o->id = id;
        o->version = version;
        o->client = c;
        c->objs[id] = o;
        return o;
    }

    void destroyObj(Client* c, uint32_t id) {
        c->objs.erase(id);
        c->deleteId(id);
    }

    void wake() {
        char b = 1;
        (void)!write(wakeW, &b, 1);
    }

    // ------------------------------------------------------------ requests
    void bind(Client* c, Args& a) {
        const uint32_t name = a.u();
        const std::string iface = a.s();
        const uint32_t version = a.u();
        const uint32_t id = a.u();
        if (a.bad || name == 0 || name > sizeof kGlobals / sizeof kGlobals[0]) return;
        const Global& g = kGlobals[name - 1];
        const uint32_t v = std::min(version, g.version);
        auto o = create<Obj>(c, id, g.type, v);
        switch (g.type) {
        case If::shm: {
            Msg f0(id, 0); f0.u(kFmtArgb); c->send(f0);
            Msg f1(id, 0); f1.u(kFmtXrgb); c->send(f1);
            break;
        }
        case If::seat: {
            Msg cap(id, 0); cap.u(1 /* pointer */); c->send(cap);
            if (v >= 2) { Msg nm(id, 1); nm.s("seat0"); c->send(nm); }
            break;
        }
        case If::output: {
            c->outputs.push_back(o);
            Msg geo(id, 0);
            geo.i(0).i(0).i(outW * 254 / 960).i(outH * 254 / 960).i(0).s("VST gHost").s("Editor").i(0);
            c->send(geo);
            Msg mode(id, 1); mode.u(3).i(outW).i(outH).i(60000); c->send(mode);
            if (v >= 2) { Msg sc(id, 3); sc.i(1); c->send(sc); Msg dn(id, 2); c->send(dn); }
            break;
        }
        default: break;
        }
    }

    void sendConfigure(Client* c, SurfaceObj& s) {
        auto tl = s.xdgToplevel.lock();
        auto xs = s.xdgSurface.lock();
        if (!tl || !xs) return;
        Msg cfg(tl->id, 0);
        cfg.i(0).i(0).arr({4 /* activated */});
        c->send(cfg);
        Msg sc(xs->id, 0);
        sc.u(serial++);
        c->send(sc);
        s.configured = true;
    }

    void commit(Client* c, const std::shared_ptr<SurfaceObj>& s) {
        ++commits;
        if (s->pAttach) {
            s->pAttach = false;
            auto b = s->pBuffer.lock();
            if (b) {
                const size_t need = (size_t)b->offset + (size_t)b->stride * (size_t)b->h;
                if (b->pool && b->pool->mem != MAP_FAILED && need <= b->pool->size && b->w > 0 && b->h > 0 && b->stride >= b->w * 4) {
                    s->bw = b->w;
                    s->bh = b->h;
                    s->argb = b->format == kFmtArgb;
                    s->px.resize((size_t)b->w * b->h);
                    const uint8_t* base = static_cast<const uint8_t*>(b->pool->mem) + b->offset;
                    for (int y = 0; y < b->h; ++y) memcpy(&s->px[(size_t)y * b->w], base + (size_t)y * b->stride, (size_t)b->w * 4);
                    if (!s->mapped) {
                        s->mapped = true;
                        s->mapSeq = ++mapCounter;
                    }
                } else {
                    ++protoErrors;
                }
                Msg rel(b->id, 0);  // we copied it: the client may reuse it right away
                c->send(rel);
            } else {
                s->mapped = false;
                s->px.clear();
                s->bw = s->bh = 0;
            }
        }
        s->scale = s->pScale;
        if (s->pInputSet) {
            s->pInputSet = false;
            s->inputInfinite = s->pInputInfinite;
            s->input = s->pInput;
        }
        s->vp = s->pVp;
        for (auto& f : s->pFrames) readyFrames.push_back(f);
        s->pFrames.clear();
        // xdg: the first commit (before any buffer) asks for the initial configure.
        if (s->role == SurfaceObj::Toplevel && !s->configured) sendConfigure(c, *s);
        dirty();
    }

    void request(Client* c, uint32_t id, uint16_t op, Args& a) {
        auto it = c->objs.find(id);
        if (it == c->objs.end()) return;  // object already destroyed: ignore
        ObjP o = it->second;
        switch (o->type) {
        case If::display:
            if (op == 0) {  // sync
                uint32_t cb = a.u();
                Msg done(cb, 0); done.u(serial++); c->send(done);
                c->deleteId(cb);
            } else if (op == 1) {  // get_registry
                uint32_t rid = a.u();
                create<Obj>(c, rid, If::registry, 1);
                for (uint32_t n = 0; n < sizeof kGlobals / sizeof kGlobals[0]; ++n) {
                    Msg g(rid, 0);
                    g.u(n + 1).s(kGlobals[n].name).u(kGlobals[n].version);
                    c->send(g);
                }
            }
            break;
        case If::registry:
            if (op == 0) bind(c, a);
            break;
        case If::compositor:
            if (op == 0) create<SurfaceObj>(c, a.u(), If::surface, o->version);
            else if (op == 1) create<RegionObj>(c, a.u(), If::region, 1);
            break;
        case If::region: {
            auto r = std::static_pointer_cast<RegionObj>(o);
            if (op == 0) destroyObj(c, id);
            else if (op == 1 || op == 2) {
                Rect rc{a.in(), a.in(), a.in(), a.in()};
                r->ops.push_back({op == 1, rc});
            }
            break;
        }
        case If::surface: {
            auto s = std::static_pointer_cast<SurfaceObj>(o);
            switch (op) {
            case 0:  // destroy
                s->mapped = false;
                destroyObj(c, id);
                dirty();
                break;
            case 1: {  // attach
                uint32_t bid = a.u();
                a.in(); a.in();
                s->pAttach = true;
                s->pBuffer = bid ? get<BufferObj>(c, bid, If::buffer) : nullptr;
                break;
            }
            case 3: {  // frame
                auto cb = create<Obj>(c, a.u(), If::callback, 1);
                s->pFrames.push_back(cb);
                break;
            }
            case 5: {  // set_input_region
                uint32_t rid = a.u();
                s->pInputSet = true;
                auto r = rid ? get<RegionObj>(c, rid, If::region) : nullptr;
                s->pInputInfinite = !r;
                s->pInput = r ? r->ops : RegionOps{};
                break;
            }
            case 6: commit(c, s); break;
            case 8: s->pScale = std::max(1, a.in()); break;
            default: break;  // damage, opaque region, transform, damage_buffer: not needed
            }
            break;
        }
        case If::subcompositor:
            if (op == 0) destroyObj(c, id);
            else if (op == 1) {
                uint32_t nid = a.u(), sid = a.u(), pid = a.u();
                auto sub = create<SubsurfaceObj>(c, nid, If::subsurface, 1);
                auto s = get<SurfaceObj>(c, sid, If::surface), p = get<SurfaceObj>(c, pid, If::surface);
                if (s && p) {
                    sub->surface = s;
                    s->role = SurfaceObj::Sub;
                    s->parent = p;
                    s->subX = s->subY = 0;
                    p->children.push_back(s);
                    dirty();
                }
            }
            break;
        case If::subsurface: {
            auto sub = std::static_pointer_cast<SubsurfaceObj>(o);
            auto s = sub->surface.lock();
            if (op == 0) {
                if (s) {
                    if (auto p = s->parent.lock()) {
                        auto& ch = p->children;
                        ch.erase(std::remove_if(ch.begin(), ch.end(), [&](const SurfaceW& w) { return w.lock() == s; }), ch.end());
                    }
                    s->parent.reset();
                    s->role = SurfaceObj::None;
                }
                destroyObj(c, id);
                dirty();
            } else if (op == 1) {
                int x = a.in(), y = a.in();
                if (s) { s->subX = x; s->subY = y; dirty(); }
            } else if (op == 2 || op == 3) {  // place_above / place_below
                uint32_t sib = a.u();
                auto sibling = get<SurfaceObj>(c, sib, If::surface);
                auto p = s ? s->parent.lock() : nullptr;
                if (s && p && sibling) {
                    auto& ch = p->children;
                    ch.erase(std::remove_if(ch.begin(), ch.end(), [&](const SurfaceW& w) { return w.lock() == s; }), ch.end());
                    auto pos = std::find_if(ch.begin(), ch.end(), [&](const SurfaceW& w) { return w.lock() == sibling; });
                    // Wine re-places a popup "above the parent" whenever it moves it; by the letter of the
                    // spec that is the bottom child, but for us the newest popup belongs on top.
                    if (sibling == p) { if (op == 2) ch.push_back(s); else ch.insert(ch.begin(), s); }
                    else if (pos == ch.end()) ch.push_back(s);
                    else ch.insert(op == 2 ? pos + 1 : pos, s);
                    dirty();
                }
            }
            break;
        }
        case If::shm:
            if (op == 0) {  // create_pool
                uint32_t nid = a.u();
                int fd = a.fd();
                int32_t size = a.in();
                auto po = create<PoolObj>(c, nid, If::shm_pool, 1);
                po->pool = std::make_shared<Pool>();
                po->pool->fd = fd;
                if (fd >= 0 && size > 0) {
                    po->pool->mem = mmap(nullptr, (size_t)size, PROT_READ, MAP_SHARED, fd, 0);
                    po->pool->size = po->pool->mem == MAP_FAILED ? 0 : (size_t)size;
                }
            }
            break;
        case If::shm_pool: {
            auto po = std::static_pointer_cast<PoolObj>(o);
            if (op == 0) {
                uint32_t nid = a.u();
                auto b = create<BufferObj>(c, nid, If::buffer, 1);
                b->pool = po->pool;
                b->offset = a.in(); b->w = a.in(); b->h = a.in(); b->stride = a.in(); b->format = a.u();
            } else if (op == 1) {
                destroyObj(c, id);  // buffers keep the mapping alive through shared_ptr
            } else if (op == 2) {
                int32_t size = a.in();
                auto& pl = *po->pool;
                if (size > 0 && pl.fd >= 0 && (size_t)size > pl.size) {
                    // Remap into a new Pool so existing buffers that captured the old one stay valid.
                    auto np = std::make_shared<Pool>();
                    np->fd = dup(pl.fd);
                    np->mem = mmap(nullptr, (size_t)size, PROT_READ, MAP_SHARED, np->fd, 0);
                    np->size = np->mem == MAP_FAILED ? 0 : (size_t)size;
                    po->pool = np;
                }
            }
            break;
        }
        case If::buffer:
            if (op == 0) destroyObj(c, id);
            break;
        case If::wm_base:
            if (op == 0) destroyObj(c, id);
            else if (op == 1) create<Obj>(c, a.u(), If::positioner, o->version);
            else if (op == 2) {
                uint32_t nid = a.u(), sid = a.u();
                auto xs = create<XdgSurfaceObj>(c, nid, If::xdg_surface, o->version);
                if (auto s = get<SurfaceObj>(c, sid, If::surface)) { xs->surface = s; s->xdgSurface = xs; }
            }
            break;
        case If::positioner:
            if (op == 0) destroyObj(c, id);
            break;
        case If::xdg_surface: {
            auto xs = std::static_pointer_cast<XdgSurfaceObj>(o);
            if (op == 0) destroyObj(c, id);
            else if (op == 1) {
                auto tl = create<XdgToplevelObj>(c, a.u(), If::xdg_toplevel, o->version);
                tl->xdgSurface = xs;
                if (auto s = xs->surface.lock()) {
                    tl->surface = s;
                    s->role = SurfaceObj::Toplevel;
                    s->xdgToplevel = tl;
                    s->configured = false;
                    toplevels.push_back(s);
                }
            } else if (op == 2) {
                create<Obj>(c, a.u(), If::xdg_popup, o->version);  // not used by Wine; never shown
            }
            break;
        }
        case If::xdg_toplevel: {
            auto tl = std::static_pointer_cast<XdgToplevelObj>(o);
            if (op == 0) {
                if (auto s = tl->surface.lock()) { s->mapped = false; s->role = SurfaceObj::None; }
                destroyObj(c, id);
                dirty();
            } else if (op == 2) {
                std::string t = a.s();
                if (auto s = tl->surface.lock()) s->title = t;
            }
            break;
        }
        case If::xdg_popup:
            if (op == 0) destroyObj(c, id);
            break;
        case If::viewporter:
            if (op == 0) destroyObj(c, id);
            else if (op == 1) {
                uint32_t nid = a.u(), sid = a.u();
                auto v = create<ViewportObj>(c, nid, If::viewport, 1);
                v->surface = get<SurfaceObj>(c, sid, If::surface);
            }
            break;
        case If::viewport: {
            auto v = std::static_pointer_cast<ViewportObj>(o);
            auto s = v->surface.lock();
            if (op == 0) {
                if (s) s->pVp = ViewportState{};
                destroyObj(c, id);
            } else if (op == 1 && s) {
                double x = fromFixed(a.in()), y = fromFixed(a.in()), w = fromFixed(a.in()), h = fromFixed(a.in());
                s->pVp.src = !(x == -1 && y == -1 && w == -1 && h == -1);
                s->pVp.sx = x; s->pVp.sy = y; s->pVp.sw = w; s->pVp.sh = h;
            } else if (op == 2 && s) {
                int w = a.in(), h = a.in();
                s->pVp.dst = !(w == -1 && h == -1);
                s->pVp.dw = w; s->pVp.dh = h;
            }
            break;
        }
        case If::seat:
            if (op == 0) c->pointers.push_back(create<Obj>(c, a.u(), If::pointer, o->version));
            else if (op == 1) create<Obj>(c, a.u(), If::keyboard, o->version);  // no keyboard capability; never used
            else if (op == 2) create<Obj>(c, a.u(), If::touch, o->version);
            else if (op == 3) destroyObj(c, id);
            break;
        case If::pointer:
            if (op == 0) {  // set_cursor: the cursor surface just never gets shown
                a.u();
                uint32_t sid = a.u();
                if (auto s = sid ? get<SurfaceObj>(c, sid, If::surface) : nullptr) s->role = SurfaceObj::Cursor;
            } else if (op == 1) destroyObj(c, id);
            break;
        case If::keyboard:
        case If::touch:
        case If::output:
            if ((o->type == If::output && op == 0) || (o->type != If::output && op == 0)) destroyObj(c, id);
            break;
        default:
            break;
        }
        if (a.bad) ++protoErrors;
    }

    // ------------------------------------------------------------ I/O
    void readClient(Client* c) {
        uint8_t buf[65536];
        char cmsgBuf[CMSG_SPACE(sizeof(int) * 28)];
        for (;;) {
            iovec iov{buf, sizeof buf};
            msghdr mh{};
            mh.msg_iov = &iov;
            mh.msg_iovlen = 1;
            mh.msg_control = cmsgBuf;
            mh.msg_controllen = sizeof cmsgBuf;
            ssize_t n = recvmsg(c->fd, &mh, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno != EAGAIN && errno != EWOULDBLOCK) c->dead = true;
                break;
            }
            if (n == 0) { c->dead = true; break; }
            for (cmsghdr* cm = CMSG_FIRSTHDR(&mh); cm; cm = CMSG_NXTHDR(&mh, cm)) {
                if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS) {
                    const size_t nfd = (cm->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                    const int* f = reinterpret_cast<const int*>(CMSG_DATA(cm));
                    for (size_t k = 0; k < nfd; ++k) c->fds.push_back(f[k]);
                }
            }
            c->in.insert(c->in.end(), buf, buf + n);
        }
        size_t off = 0;
        while (c->in.size() - off >= 8) {
            uint32_t hdr[2];
            memcpy(hdr, &c->in[off], 8);
            const uint32_t size = hdr[1] >> 16;
            const uint16_t op = (uint16_t)(hdr[1] & 0xFFFF);
            if (size < 8 || (size & 3)) { c->dead = true; break; }
            if (c->in.size() - off < size) break;
            std::vector<uint32_t> words((size - 8) / 4);
            if (!words.empty()) memcpy(words.data(), &c->in[off + 8], size - 8);
            Args a{words.data(), words.size(), 0, c};
            request(c, hdr[0], op, a);
            off += size;
        }
        c->in.erase(c->in.begin(), c->in.begin() + (ptrdiff_t)off);
    }

    void flush(Client* c) {
        while (!c->out.empty() && !c->dead) {
            ssize_t n = ::send(c->fd, c->out.data(), c->out.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno != EAGAIN && errno != EWOULDBLOCK) c->dead = true;
                break;
            }
            c->out.erase(c->out.begin(), c->out.begin() + n);
        }
        if (c->out.size() > (8u << 20)) c->dead = true;  // client stopped reading
    }

    void fireFrames() {
        if (readyFrames.empty()) return;
        const uint32_t t = nowMs();
        for (auto& w : readyFrames) {
            auto cb = w.lock();
            if (!cb || !alive(cb.get())) continue;
            Client* c = cb->client;
            if (c->objs.count(cb->id) == 0) continue;
            Msg done(cb->id, 0);
            done.u(t);
            c->send(done);
            destroyObj(c, cb->id);
        }
        readyFrames.clear();
    }

    void loop() {
        uint32_t lastFrame = nowMs();
        while (run.load()) {
            std::vector<pollfd> pf;
            {
                std::lock_guard<std::mutex> lk(m);
                pf.push_back({listenFd, POLLIN, 0});
                pf.push_back({wakeR, POLLIN, 0});
                for (auto& c : clients) pf.push_back({c->fd, (short)(POLLIN | (c->out.empty() ? 0 : POLLOUT)), 0});
            }
            int r = poll(pf.data(), pf.size(), 16);
            if (r < 0 && errno != EINTR) break;
            std::lock_guard<std::mutex> lk(m);
            if (pf[1].revents & POLLIN) {
                char b[64];
                while (read(wakeR, b, sizeof b) > 0) {}
            }
            if (pf[0].revents & POLLIN) {
                for (;;) {
                    int fd = accept4(listenFd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
                    if (fd < 0) break;
                    auto c = std::make_unique<Client>();
                    c->fd = fd;
                    auto disp = std::make_shared<Obj>();
                    disp->type = If::display;
                    disp->id = 1;
                    disp->client = c.get();
                    c->objs[1] = disp;
                    clients.push_back(std::move(c));
                    ++connects;
                }
            }
            for (size_t k = 2; k < pf.size(); ++k) {
                if (k - 2 >= clients.size()) break;
                Client* c = clients[k - 2].get();
                if (c->fd != pf[k].fd) continue;
                if (pf[k].revents & (POLLIN | POLLHUP | POLLERR)) readClient(c);
            }
            const uint32_t t = nowMs();
            if (t - lastFrame >= 16) { lastFrame = t; fireFrames(); }
            for (auto& c : clients) flush(c.get());
            for (auto& c : clients)
                if (c->dead)
                    for (auto& [id, o] : c->objs) o->client = nullptr;  // anything still referenced is now orphaned
            const size_t before = clients.size();
            clients.erase(std::remove_if(clients.begin(), clients.end(), [](const std::unique_ptr<Client>& c) { return c->dead; }), clients.end());
            if (clients.size() != before) {
                // Dropping a client releases its objects; forget surfaces that went with it.
                toplevels.erase(std::remove_if(toplevels.begin(), toplevels.end(), [](const SurfaceW& w) { return w.expired(); }), toplevels.end());
                readyFrames.clear();
                if (shownTop && !shownTop->client) shownTop.reset();
                if (auto f = focus.lock(); f && !f->client) { focus.reset(); grabbed = false; }
                dirty();
            }
        }
    }

    // ------------------------------------------------------------ scene
    std::shared_ptr<SurfaceObj> currentTop() {
        std::shared_ptr<SurfaceObj> best;
        for (auto& w : toplevels) {
            auto s = w.lock();
            if (!s || !alive(s.get()) || s->role != SurfaceObj::Toplevel || !s->mapped || s->bw < 8 || s->bh < 8) continue;
            if (!best || s->mapSeq > best->mapSeq) best = s;
        }
        return best;
    }

    void collect(const std::shared_ptr<SurfaceObj>& s, int x, int y, std::vector<Placed>& out, int depth) {
        if (depth > 16 || !s->mapped) return;
        out.push_back({s, x, y, s->logicalW(), s->logicalH()});
        for (auto& w : s->children)
            if (auto ch = w.lock()) collect(ch, x + ch->subX, y + ch->subY, out, depth + 1);
    }

    static uint32_t blend(uint32_t dst, uint32_t src) {  // premultiplied ARGB over
        const uint32_t a = src >> 24;
        if (a == 255) return src;
        if (a == 0) return dst;
        const uint32_t ia = 255 - a;
        uint32_t rb = ((dst & 0x00FF00FF) * ia >> 8) & 0x00FF00FF;
        uint32_t g = ((dst & 0x0000FF00) * ia >> 8) & 0x0000FF00;
        return (src + rb + g) | 0xFF000000u;
    }

    void compose(Snapshot& out) {
        out.argb.clear();
        out.width = out.height = 0;
        out.title.clear();
        auto top = currentTop();
        shownTop = top;
        if (!top) return;
        std::vector<Placed> pl;
        collect(top, 0, 0, pl, 0);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        for (auto& p : pl) {
            x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x + p.w); y1 = std::max(y1, p.y + p.h);
        }
        const int W = std::min(x1 - x0, 8192), H = std::min(y1 - y0, 8192);
        if (W <= 0 || H <= 0) return;
        originX = x0;
        originY = y0;
        out.width = W;
        out.height = H;
        out.title = top->title;
        out.argb.assign((size_t)W * H, 0xFF000000u);
        for (auto& p : pl) {
            SurfaceObj& s = *p.s;
            if (s.px.empty() || p.w <= 0 || p.h <= 0) continue;
            const double sc = std::max(s.scale, 1);
            const double sx = s.vp.src ? s.vp.sx * sc : 0, sy = s.vp.src ? s.vp.sy * sc : 0;
            const double sw = s.vp.src ? s.vp.sw * sc : s.bw, sh = s.vp.src ? s.vp.sh * sc : s.bh;
            const bool identity = sx == 0 && sy == 0 && (int)sw == p.w && (int)sh == p.h && sw == s.bw && sh == s.bh;
            for (int ly = 0; ly < p.h; ++ly) {
                const int cy = p.y + ly - y0;
                if (cy < 0 || cy >= H) continue;
                const int by = identity ? ly : (int)(sy + (ly + 0.5) * sh / p.h);
                if (by < 0 || by >= s.bh) continue;
                uint32_t* dst = &out.argb[(size_t)cy * W];
                const uint32_t* row = &s.px[(size_t)by * s.bw];
                for (int lx = 0; lx < p.w; ++lx) {
                    const int cx = p.x + lx - x0;
                    if (cx < 0 || cx >= W) continue;
                    const int bx = identity ? lx : (int)(sx + (lx + 0.5) * sw / p.w);
                    if (bx < 0 || bx >= s.bw) continue;
                    const uint32_t v = row[bx];
                    dst[cx] = s.argb ? blend(dst[cx], v) : (v | 0xFF000000u);
                }
            }
        }
    }

    // Topmost surface under canvas point (x, y) and the point in its local coordinates.
    std::shared_ptr<SurfaceObj> hit(double x, double y, double& lx, double& ly) {
        auto top = shownTop && shownTop->mapped && alive(shownTop.get()) ? shownTop : currentTop();
        if (!top) return nullptr;
        std::vector<Placed> pl;
        collect(top, 0, 0, pl, 0);
        const double gx = x + originX, gy = y + originY;
        for (auto it = pl.rbegin(); it != pl.rend(); ++it) {
            const double px = gx - it->x, py = gy - it->y;
            if (px < 0 || py < 0 || px >= it->w || py >= it->h) continue;
            if (!it->s->inputInfinite && !regionContains(it->s->input, (int)px, (int)py)) continue;
            lx = px;
            ly = py;
            return it->s;
        }
        return nullptr;
    }

    void localOf(const std::shared_ptr<SurfaceObj>& target, double x, double y, double& lx, double& ly) {
        // Position of `target` in canvas space: walk up its parents.
        int ox = 0, oy = 0;
        for (auto s = target; s; s = s->parent.lock()) {
            if (s->role != SurfaceObj::Sub) break;
            ox += s->subX;
            oy += s->subY;
        }
        lx = x + originX - ox;
        ly = y + originY - oy;
    }

    void pointerEvents(Client* c, const std::function<void(uint32_t ptrId, uint32_t version)>& fn) {
        for (auto& w : c->pointers)
            if (auto p = w.lock())
                if (c->objs.count(p->id)) fn(p->id, p->version);
    }

    void moveTo(double x, double y) {
        double lx = 0, ly = 0;
        std::shared_ptr<SurfaceObj> target;
        auto cur = focus.lock();
        if (grabbed && cur && cur->mapped) {
            target = cur;
            localOf(target, x, y, lx, ly);
        } else {
            target = hit(x, y, lx, ly);
        }
        const uint32_t t = nowMs();
        if (target != cur) {
            if (cur && alive(cur.get()) && cur->client->objs.count(cur->id)) {
                const uint32_t sr = serial++;
                pointerEvents(cur->client, [&](uint32_t pid, uint32_t v) {
                    Msg lv(pid, 1); lv.u(sr).u(cur->id); cur->client->send(lv);
                    if (v >= 5) { Msg fr(pid, 5); cur->client->send(fr); }
                });
            }
            focus = target;
            if (target && alive(target.get())) {
                const uint32_t sr = serial++;
                pointerEvents(target->client, [&](uint32_t pid, uint32_t v) {
                    Msg en(pid, 0); en.u(sr).u(target->id).f(lx).f(ly); target->client->send(en);
                    if (v >= 5) { Msg fr(pid, 5); target->client->send(fr); }
                });
            }
        } else if (target && alive(target.get())) {
            pointerEvents(target->client, [&](uint32_t pid, uint32_t v) {
                Msg mo(pid, 2); mo.u(t).f(lx).f(ly); target->client->send(mo);
                if (v >= 5) { Msg fr(pid, 5); target->client->send(fr); }
            });
        }
    }

    void button(bool down) {
        auto cur = focus.lock();
        grabbed = down && cur;
        if (!cur || !alive(cur.get())) return;
        const uint32_t sr = serial++, t = nowMs();
        pointerEvents(cur->client, [&](uint32_t pid, uint32_t v) {
            Msg b(pid, 3); b.u(sr).u(t).u(kBtnLeft).u(down ? 1 : 0); cur->client->send(b);
            if (v >= 5) { Msg fr(pid, 5); cur->client->send(fr); }
        });
    }
};

Compositor::Compositor() : d(new Impl) {}
Compositor::~Compositor() { stop(); }

bool Compositor::start(const std::string& socketPath, int outputW, int outputH, std::string& err) {
    if (d->run.load()) return true;
    d->path = socketPath;
    d->outW = outputW;
    d->outH = outputH;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socketPath.size() >= sizeof addr.sun_path) { err = "socket path too long"; return false; }
    memcpy(addr.sun_path, socketPath.c_str(), socketPath.size() + 1);
    unlink(socketPath.c_str());
    d->listenFd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (d->listenFd < 0) { err = std::string("socket: ") + strerror(errno); return false; }
    if (bind(d->listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || listen(d->listenFd, 16) < 0) {
        err = std::string("bind/listen: ") + strerror(errno);
        close(d->listenFd);
        d->listenFd = -1;
        return false;
    }
    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) { err = "pipe2 failed"; close(d->listenFd); d->listenFd = -1; return false; }
    d->wakeR = p[0];
    d->wakeW = p[1];
    d->run = true;
    d->th = std::thread([this] { d->loop(); });
    return true;
}

void Compositor::stop() {
    if (!d->run.exchange(false)) return;
    d->wake();
    if (d->th.joinable()) d->th.join();
    std::lock_guard<std::mutex> lk(d->m);
    d->clients.clear();
    d->toplevels.clear();
    d->shownTop.reset();
    close(d->listenFd);
    close(d->wakeR);
    close(d->wakeW);
    d->listenFd = d->wakeR = d->wakeW = -1;
    unlink(d->path.c_str());
}

bool Compositor::running() const { return d->run.load(); }

bool Compositor::snapshot(uint64_t haveSerial, Snapshot& out) {
    std::lock_guard<std::mutex> lk(d->m);
    if (haveSerial == d->sceneSerial) return false;
    d->compose(out);
    out.serial = d->sceneSerial;
    return true;
}

void Compositor::pointer(double x, double y, int button) {
    {
        std::lock_guard<std::mutex> lk(d->m);
        d->moveTo(x, y);
        if (button >= 0) d->button(button == 1);
    }
    d->wake();
}

void Compositor::scroll(double notches) {
    {
        std::lock_guard<std::mutex> lk(d->m);
        auto cur = d->focus.lock();
        if (!cur || !alive(cur.get())) return;
        const uint32_t t = nowMs();
        const int discrete = (int)std::lround(notches);
        d->pointerEvents(cur->client, [&](uint32_t pid, uint32_t v) {
            if (v >= 5) {
                Msg src(pid, 6); src.u(0 /* wheel */); cur->client->send(src);
                Msg dis(pid, 8); dis.u(0).i(discrete); cur->client->send(dis);
            }
            Msg ax(pid, 4); ax.u(t).u(0 /* vertical */).f(notches * 10.0); cur->client->send(ax);
            if (v >= 5) { Msg fr(pid, 5); cur->client->send(fr); }
        });
    }
    d->wake();
}

void Compositor::closeShown() {
    {
        std::lock_guard<std::mutex> lk(d->m);
        auto top = d->currentTop();
        if (!top) return;
        if (auto tl = top->xdgToplevel.lock()) {
            Msg cl(tl->id, 1);
            top->client->send(cl);
        }
    }
    d->wake();
}

std::string Compositor::stats() {
    std::lock_guard<std::mutex> lk(d->m);
    size_t objs = 0, mapped = 0;
    for (auto& c : d->clients) {
        objs += c->objs.size();
        for (auto& [id, o] : c->objs)
            if (o->type == If::surface && std::static_pointer_cast<SurfaceObj>(o)->mapped) ++mapped;
    }
    auto top = d->currentTop();
    char b[320];
    snprintf(b, sizeof b,
             "{\"clients\":%zu,\"connects\":%llu,\"objects\":%zu,\"mappedSurfaces\":%zu,\"toplevels\":%zu,\"commits\":%llu,"
             "\"protoErrors\":%llu,\"shown\":\"%dx%d\"}",
             d->clients.size(), (unsigned long long)d->connects, objs, mapped, d->toplevels.size(), (unsigned long long)d->commits,
             (unsigned long long)d->protoErrors, top ? top->logicalW() : 0, top ? top->logicalH() : 0);
    return b;
}

}  // namespace vgwl
