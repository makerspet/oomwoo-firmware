# Canonical protocol snapshot

`protocol_v1.json` and `golden_vectors_v1.json` are copied without modification
from `makerspet/oomwoo` commit
`90324ec79491a1f71eaadd86fce0d92b0e13c15a`, where the CPU/MCU contract and its
23 vectors were merged in `makerspet/oomwoo#63`.

Regenerate the C fixture after updating either snapshot:

```bash
python3 tools/generate_message_vectors.py
```

CI runs the generator in `--check` mode so a contract update cannot silently
leave the firmware fixture stale.

## Command-gate corpus

`command_gate_vectors_v1.json` is a vendored copy of the language-neutral
accept/reject corpus authored in `makerspet/oomwoo`
(`contributions/io-board-interface/smailzhu`, commit `d83c13f`, PR #70). Its 57
vectors pin how the CPU->MCU command gate must accept or reject each decoded
frame. `tests/command_gate_conformance.c` runs them against
`oomwoo_cpu_ingress_validate_frame`, so the firmware gate and the upstream host
oracle cannot drift.

The copy is integrity-pinned: `tools/generate_gate_vectors.py` refuses to run
unless the file's SHA-256 matches the pin, then emits the C fixture.

```bash
python3 tools/generate_gate_vectors.py
```

Update `EXPECTED_SHA256` (and review the corpus) only when deliberately syncing
a newer upstream revision.
