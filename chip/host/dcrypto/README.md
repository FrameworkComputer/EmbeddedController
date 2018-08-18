# Rough Dcrypto Implementation on Host for Fuzzing Targets.

This implementation of the dcrypto API is not complete, but provides the needed
function definitions to fuzz Cr50 code.
The the following should be noted:
* A complete implementation of dcrypto does not add any extra coverage since the
  dcrypto code here doesn't match the Cr50 implementation that depends on
  a specific hardware accelerator.
* The input data comes from a fuzzer so storage encryption isn't necessary—no
  user data is handled.
* For fuzzing fully implementing the crypto functionality isn't useful for the
  purpose of finding bugs--it makes the fuzzer take longer to execute without
  providing any benefit for the extra cycles.
