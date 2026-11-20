# AI Toolkit

Source: https://github.com/linkdd/aitoolkit
Revision: `a55f6089c2421d1202aee7e15901686c25a26142`
License: MIT, reproduced in `LICENSE.txt`.

Only the unmodified `include/aitoolkit/utility.hpp` module is vendored.
FRAGNAROK uses its evaluator through `game/ai_utility.cpp`, compiled as C++20
in the native standalone game module. No network fetch is needed to build.
All action scores are positive because the upstream evaluator initializes its
best score with `numeric_limits<float>::min()`.
