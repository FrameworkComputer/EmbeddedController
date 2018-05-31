Proving P256 dcrypto code
=========================

In 2018, partial proofs of modular reduction were written in the Coq proof
assistant.
They can be used against the crypto accelerator code in [chip/g/dcrypto/dcrypto_p256.c](dcrypto_p256.c).

The Coq code is in this file:
[github.com/mit-plv/fiat-crypto/.../Experiments/SimplyTypedArithmetic.v](https://github.com/mit-plv/fiat-crypto/blob/e469076c37fc8b1b6d66eb700e379b9b2a093cb7/src/Experiments/SimplyTypedArithmetic.v)

Specific lines of interest:

Instruction specifications:
[fiat-crypto/.../Experiments/SimplyTypedArithmetic.v#L10014](https://github.com/mit-plv/fiat-crypto/blob/e469076c37fc8b1b6d66eb700e379b9b2a093cb7/src/Experiments/SimplyTypedArithmetic.v#L10014)

Printouts of verified code versions with explanatory comments are at the very
end of the same file (which GitHub cuts off, so here is the link to the raw
version):
https://raw.githubusercontent.com/mit-plv/fiat-crypto/e469076c37fc8b1b6d66eb700e379b9b2a093cb7/src/Experiments/SimplyTypedArithmetic.v

Additionally, the MulMod procedure in p256 uses a non-standard Barrett
reduction optimization. In particular, it assumes that the quotient estimate is
off by no more than 1, while most resources say it can be off by 2. This
assumption was proven correct for most primes (including p256) here:

[fiat-crypto/.../Arithmetic/BarrettReduction/Generalized.v#L140](https://github.com/mit-plv/fiat-crypto/blob/e469076c37fc8b1b6d66eb700e379b9b2a093cb7/src/Arithmetic/BarrettReduction/Generalized.v#L140)

The proofs can be re-checked using Coq version 8.7 or 8.8 (or above, probably).
