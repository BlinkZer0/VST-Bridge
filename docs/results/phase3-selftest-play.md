# VST gHost: Phase 3 self-test (virtual MIDI devices)

Generated 2026-11-18 11:14:39 on samsung SM-S948U1, Android 17, edition **play**.

Slot assignments: VST gHost Inst 1 = E-Piano, VST gHost Inst 2 = Odin2, VST gHost FX 1 = Pedal Faster

- Android MIDI service lists 4 VST gHost devices: VST gHost Inst 1, VST gHost Inst 2, VST gHost Inst 3, VST gHost Inst 4
- opened 2 devices via MidiManager.openDevice + openInputPort(0)
- VST gHost Inst 1: host running 1927 ms after the first MIDI note
- VST gHost Inst 2: host running 2668 ms after the first MIDI note

| Slot | VST | MIDI messages consumed by the host | Output peak (app mix) | Underruns | Result |
|---|---|---|---|---|---|
| VST gHost Inst 1 | E-Piano | 96 | -14.2 dBFS | 0 | **PASS** |
| VST gHost Inst 2 | Odin2 | 96 | -0.1 dBFS | 0 | **PASS** |

- 96 note-ons sent over 4.617 s to 2 devices at the same time (multi-slot mix)
- overall: **PASS**
