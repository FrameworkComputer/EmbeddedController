The rma_key_blob.{prod,test} files in this directory are 33 byte binary blobs
concatenating the 32 byte of respective public key used by prod or test RMA
server and one byte of the key ID.

The util/bin2h.sh script is used to convert these binary blobs into .h
file containing a #define statement which is suitable for use in C.
