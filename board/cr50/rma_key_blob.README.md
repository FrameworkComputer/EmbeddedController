The rma_key_blob.{p256,x25519}.{prod,test} files in this directory are binary
blobs concatenating the respective public key used by prod or test RMA server
and single byte of the key ID. The key size for p256 is 65 bytes, for x25519 -
32 bytes.

The util/bin2h.sh script is used to convert these binary blobs into .h
file containing a #define statement which is suitable for use in C.
