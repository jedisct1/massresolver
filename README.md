massresolver
============

Mass DNS resolution tool

Dependencies:

- `libunbound-dev`
- `libldns-dev`

Compilation:

```bash
make
```

Usage:

Pipe the list of names to resolve to `stdin`, one entry per line.

By default, Massresolver doesn't use any upstream caching resolvers,
and solely uses `libunbound`.

This can be changed by defining `USE_LOCALHOST` or `USE_RESOLVCONF`.

Up to 640 queries will run in parallel, but this can be changed using
the `MAX_RUNNING` macro.

Even with the default number of queries, you may have to adjust the
maximum number of file descriptors allowed for your session.
Something like `ulimit -n 100000` might do the job.

