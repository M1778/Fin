# Sample authority lives in per-construct expectations, not per-file labels

Every sample carries `//@` expectations — `ok`, `error <line>:<col> "<msg>"`, or `unimplemented
"<reason>"` — and those, not the file's normative-or-aspirational label, decide whether the compiler
is at fault. The label survives as a default and a reading aid. A sample carrying no expectation is
a harness error, not a pass.

The corpus forced this. `blame_assert.fin` is ordinary normative code except line 19, which already
carries the comment `// TODO: This should be allowed`. A file-level label offers only two readings of
that file, and both are wrong: excuse the whole file and lose nineteen lines of specification, or
convict the compiler for a line the corpus itself says isn't done. Nine of fifty samples sit
somewhere on that spectrum.

The alternative considered was file-level labels plus a hand-maintained exception list. That was
rejected because the exception list is a second place the truth lives, and it goes stale silently —
a construct that starts working leaves a stale exception behind that nothing forces anyone to
notice. With expectations in the file, a construct that starts working makes its own
`unimplemented` expectation fail, and the harness demands attention.

## Consequences

All fifty samples must be annotated before the harness can run, and an unannotated file is a build
failure rather than a silent skip. That is deliberate — a sample that says nothing about what should
happen is indistinguishable from an untested one — but it means the annotation pass is a gate on the
test suite existing at all, not something that can be done incrementally.

`error` expectations pin exact line and column numbers, which makes them brittle against edits to
the sample above the error. This is the price of testing diagnostic quality rather than merely
testing that something failed, and diagnostic quality is a stated goal.

Column numbers only mean something once line endings are settled, because eleven samples are CRLF.
The lexer must treat `\r\n` as one terminator and never count `\r` as a column, or the same
expectation is right on one machine and wrong on another.

An expectation can disagree with its file's label — an `ok` inside an aspirational file, an
`unimplemented` inside a normative one. Nothing forbids it and nothing should, since that
disagreement is exactly how a construct gets promoted without relabelling the file. It does mean the
label is advisory and cannot be relied on by tooling.
