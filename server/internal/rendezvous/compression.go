package rendezvous

import (
	"github.com/PretendoNetwork/nex-go/v2/compression"
)

// ratioByte is Quazal's payload framing without compression: a leading compression-ratio
// byte, 0 on everything the server sends. Incoming compressed payloads (ratio > 0) are
// inflated with zlib.
type ratioByte struct{ zlib *compression.Zlib }

func newRatioByte() *ratioByte { return &ratioByte{zlib: compression.NewZlibCompression()} }

func (r *ratioByte) Compress(payload []byte) ([]byte, error) {
	return append([]byte{0}, payload...), nil
}

func (r *ratioByte) Decompress(payload []byte) ([]byte, error) {
	if len(payload) == 0 {
		return payload, nil
	}
	return r.zlib.Decompress(payload)
}

func (r *ratioByte) Copy() compression.Algorithm { return newRatioByte() }
