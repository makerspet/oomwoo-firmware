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
