# PTX frontend generated snapshot

These files were generated from `endingly/ptx_frontend` commit
`1c4547f65c888ee92b1933a20f9a74b380b96953`, the exact revision fetched by
the adjacent portfile. They replace that revision's Python/PyYAML/jsonschema
code-generation step so the overlay build has no pip resolver or PyPI input.
This revision adds the matrix generated payload
`private/resolved_ir_matrix.gen.cpp`.

With `BUILD_TESTING=ON`, CTest runs an automatic snapshot integrity check. It
verifies `SHA256SUMS` covers exactly the generated payloads, all payloads use
LF and the no-timestamp form, and this provenance revision matches the adjacent
portfile's `REF`; it does not run the upstream generator. Changing a payload
requires its hash update, and changing the pin requires the corresponding
provenance update.

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
diff -ruN --exclude=README.md --exclude=SHA256SUMS "$tmp" <overlay-generated-dir>
```

This documented manual regeneration/byte comparison is separate from the
automatic snapshot integrity check. An empty `diff` byte-reproduces the
generated payloads; then update the snapshot, `SHA256SUMS`, and this
provenance note in the same change.
