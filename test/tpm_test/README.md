# TPM / Crypto unit tests
tpmtest.py runs set of tests to check correctness of cryptographic functions
implementation. These tests require a special firmware image built with
CRYPTO_TEST=1 flag to enable direct exposure of low-level cryptographic
functions via TPM extensions and vendor commands. TPM functionality itself
is disabled due to not enough enough flash/memory to fit both.
As such, these tests are expected to run over H1 Red board.

Firmware image is expected to be built with:

make BOARD=cr50 CRYPTO_TEST=1 -j

Cryptographic tests are invoked when tpmtest.py is executed with no command-line
parameters:

        ./tpmtest.py

Option *-d* can be used for debugging. It adds output of actual data
sent/received from H1, which is handy when adding new functionality:

        ./tpmtest.py -d

# TRNG tests
Another functionality is statistical assessment of entropy from True Random
Number Generator (TRNG). These tests are prerequisite for FIPS 140-2/3
certification and governed by NIST SP 800-90B.
tpmtest.py implements a mode to download raw data from TRNG:

        ./tpmtest.py -t

Script nist_entropy.sh automated this testing by
1. Downloading latest NIST Entropy Assessment tool from
   [https://github.com/usnistgov/SP800-90B_EntropyAssessment] and building it.
2. Gathering 1000000 of 8-bit samples from H1 TRNG and
   storing it in /tmp/trng_output using tpmtest.py -t
3. Running NIST tool in non-IID (independent and identically distributed) mode
   to estimate entropy. This specific mode is choosed as there is no formal
   proof that TRNG data is independent and identically distributed.
   It follows manual in
   [https://nvlpubs.nist.gov/nistpubs/SpecialPublications/NIST.SP.800-90B.pdf]

The successful result is being awarded entropy estimate for TRNG, which is
expected to be more than 7 (8 is theoretical max).
If test fails, no entropy assessment is awarded.

This script is expected to run in platform/ec/test/tpm_test directory
(where ./tpmtest.py is located)


