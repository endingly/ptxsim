# PTX frontend generated snapshot

These files were generated from `endingly/ptx_frontend` commit
`bf3538f6243dcef72e6e7d2db3e209a93114f35c`, the exact revision fetched by
the adjacent portfile.  They replace that revision's Python/PyYAML/jsonschema
code-generation step so the overlay build has no pip resolver or PyPI input.

The checked-in payloads deliberately use the generator's no-timestamp form:
every generated file starts with
`// Generated at: omitted (set SOURCE_DATE_EPOCH for a reproducible timestamp)`.
Therefore `SOURCE_DATE_EPOCH` must be *unset* when regenerating.  Setting it,
including to `0`, emits a timestamp and does not reproduce this snapshot.

From a checkout of the pinned source revision, with its generator dependencies
available, regenerate and check the payloads as follows (replace
`<overlay-generated-dir>` with this directory):

```sh
tmp=$(mktemp -d)
(
  cd <ptx-frontend-source>
  env -u SOURCE_DATE_EPOCH python3 python/scripts/gen_all.py \
    --spec-dir instructions/ptx_spec \
    --backend-spec instructions/ptx_cpp_backend_spec/ptx_frontend.yaml \
    --output "$tmp"
)
diff -ruN --exclude=README.md "$tmp" <overlay-generated-dir>
```

An empty `diff` byte-reproduces the generated payloads; then update the
snapshot and this provenance note in the same change.
