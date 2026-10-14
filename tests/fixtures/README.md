# Fixed format fixtures

`record.*` represents the same object in JSON, BSON, MessagePack and TOML:
42, the UTF-8 string 日本語, true, two nested objects (1 and 2), and a nested
object containing -7. The test asserts these values explicitly.

Binary bytes were constructed directly from the format specifications without
using HOSQ. `invalid.*` contains incomplete syntax or a truncated binary payload.
These tiny files are kept in Git for offline smoke tests. Larger upstream corpora
are fetched into the Nix store; see the root README.
