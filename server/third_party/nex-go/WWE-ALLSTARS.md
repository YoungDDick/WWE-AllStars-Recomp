# nex-go, vendored

Upstream: github.com/PretendoNetwork/nex-go v2.3.2 (`18bf4cf`), AGPL-3.0 (see LICENSE).
The game server uses it through a `replace` in `server/go.mod`. Local changes:

- `prudp_packet_v0.go`: accept 11-byte PRUDP v0 packets (a PING or DISCONNECT without
  payload, with the 1-byte checksum). Upstream required 12 bytes and dropped them.
