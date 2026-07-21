# Release Licence Verification v0.1

- **Document ID:** `SWE-ENV-LIC-WP1-REL-v0.1`
- **Work Package:** `SWE-ENV-LIC-WP1`
- **Procedure owner:** `SWE-ENV-LIC`
- **Approval owner:** `SWE-REL-PKG` release approver with `SWE-ENV-LIC`
  review; component owners supply evidence

**Failure rule:** A release is blocked when any shipped component, transitive,
runtime library, plugin, Qt module, resource or dataset is absent, unclassified,
unverified or lacks its required notice/source action.

## Purpose

The dependency licence register is a baseline decision, not a permanent bill of
materials. This procedure turns the exact artefacts for a proposed release into
an approved evidence set. It applies separately to every platform, triplet,
feature combination, installer/archive and development image that will be
distributed.

The procedure does not automate packaging. Commands shown below are examples of
evidence collection; the reviewer must inspect their output and the produced
package rather than accepting a green command alone.

## Evidence identity

Open one evidence record for each release candidate and record:

```text
release/version:
source commit:
build workflow/run:
platform and architecture:
compiler/toolchain and runtime:
vcpkg executable version/tool commit:
vcpkg triplet:
manifest features:
Qt version, kit and licence route:
package/installer filename and digest:
prepared by/date:
reviewed by/date:
approved by/date:
```

Store immutable command output, manifests and approvals under the release's
controlled evidence location. A recommended repository-external artefact layout
is:

```text
release-evidence/<release>/<platform>/licensing/
|-- provenance/
|-- dependency-graphs/
|-- installed-copyright/
|-- qt-sbom/
|-- runtime-inventory/
|-- dataset-licences/
|-- notices/
`-- approvals/
```

If evidence is retained in CI rather than Git, record durable run/artifact URLs
and retention policy in the release record. Do not commit proprietary licence
agreements, credentials or personal data; record their controlled location and
review outcome.

## Release gate checklist

Every checkbox requires an evidence reference and reviewer initials/date.

### 1. Source and registry authority

- [ ] Record the exact source commit and prove the worktree used for packaging
  corresponds to it.
- [ ] Archive the release copies and digests of `vcpkg.json` and
  `vcpkg-configuration.json`.
- [ ] Confirm the sole registry authority is full commit
  `d015e31e90838a4c9dfa3eed45979bc70d9357fc`, or document an independently
  approved superseding dependency/licence change.
- [ ] Record the vcpkg executable version/tool commit separately from the package
  registry baseline.
- [ ] Reject floating branches, unreviewed overlays, undocumented registries,
  ad-hoc downloads and a second baseline authority.

Evidence: manifest/config copies, digests, vcpkg version output, source commit,
registry commit verification. Owner: `SWE-ENV-DEP`; reviewer: `SWE-ENV-LIC`.

### 2. Direct and transitive dependency graph

- [ ] Record platform, triplet and the exact enabled manifest features.
- [ ] Export or preserve the resolved package plan/status for the release install
  tree.
- [ ] Compare the direct dependency list with `vcpkg.json` and the licence
  register; explain every difference.
- [ ] Diff the complete transitive graph and feature set against the previous
  approved release/evidence baseline.
- [ ] Classify every added or changed port before proceeding; removal must also
  be checked for orphaned runtime files/notices.
- [ ] Confirm build-helper/host ports did not enter the application package.

Evidence: dependency plans/status, graph diff, change review. Owner:
`SWE-ENV-DEP`; capability owners approve intentional feature changes;
`SWE-ENV-LIC` classifies the result.

### 3. Installed vcpkg copyright metadata

- [ ] For every resolved port, capture the exact installed file at
  `<install-root>/<triplet>/share/<port>/copyright`.
- [ ] Fail if that file is absent, empty, points only to an unavailable network
  location, or does not cover the packaged content.
- [ ] Where a port uses a composite or null SPDX field (notably GDAL/netCDF-C),
  inspect upstream source, enabled components/drivers and exact installed text;
  never substitute a guessed headline licence.
- [ ] Reconcile the collected set with the generated third-party notice bundle.
- [ ] Retain port version, port revision and source-tree/provenance information
  beside each copyright file.

Evidence: copied installed metadata plus exception notes. Owner: `SWE-ENV-LIC`;
dependency/component owners resolve gaps.

### 4. Qt modules and deployment

- [ ] Record the exact installed Qt version, kit, acquisition route and whether
  commercial or open-source terms are being used.
- [ ] Capture the installed Qt SPDX SBOM and third-party attribution data.
- [ ] Inventory each deployed module separately. At minimum check Core, Gui, Qml,
  Quick, Quick Controls and Concurrent; absence is acceptable only when link and
  deployment evidence confirms it is unused.
- [ ] Inventory QML imports, control styles, platform/image plugins, software
  rendering components and other deployment-tool output—not only DLL/shared
  library names.
- [ ] Fail on an unapproved or GPL-only module under a non-GPL product licence.
- [ ] For the open-source LGPL route, confirm dynamic linkage, required prominent
  notices/licence text, corresponding Qt source or compliant offer/instructions,
  user relinking/reverse-engineering rights and any required installation
  information.
- [ ] If static Qt or a commercial route is proposed, stop and obtain a specific
  legal/licence approval rather than reusing the dynamic-LGPL decision.

Evidence: Qt SBOM, installer provenance, deployment log/tree, link/runtime scan,
licence-route approval and source-availability material. Owners: `SWE-GUI`,
`SWE-REL-PKG`, `SWE-ENV-LIC`.

### 5. Runtime-library and package-content inventory

- [ ] Generate a recursive file inventory and cryptographic digest list for the
  release archive/installer output.
- [ ] Inspect actual executable/library dependencies with platform-appropriate
  tooling (`ldd`/ELF metadata, `otool` on macOS, or PE/import and deployment
  tooling on Windows).
- [ ] Map every non-system runtime library, plugin, QML import, codec, database,
  grid and resource back to a register row and exact notice.
- [ ] Verify compiler runtimes (GCC/LLVM/MSVC), C/C++ runtimes and Qt/vcpkg shared
  libraries are distributable under the selected packaging route.
- [ ] Check that static/header-embedded components still appear in notices even
  though a runtime scanner cannot see them.
- [ ] Check that debug libraries, development headers/tools and build caches are
  absent unless the product is intentionally a reviewed SDK distribution.

Evidence: package tree/digests, linker/import reports and component mapping.
Owners: `SWE-REL-PKG`, `SWE-ENV-BLD`, `SWE-ENV-LIC`.

### 6. Optional features and external-tool exclusion

- [ ] Confirm the package's feature set. Do not include Matplot++, CImg, nodesoup,
  netCDF-C or their runtimes/notices unless the corresponding feature is enabled.
- [ ] If diagnostics is enabled, prove the approved backend exists and is
  documented. Gnuplot remains excluded/unapproved for bundling until its
  stack/acquisition/licence/smoke decision completes.
- [ ] Confirm Gmsh remains an external process and no Gmsh library/code is linked
  or bundled.
- [ ] Confirm ParaView, QGIS, MATLAB, Doxygen, LaTeX, Python, Git, CMake, Ninja,
  vcpkg, clang-tidy, ccache/sccache and compiler executables are excluded from
  the desktop application.
- [ ] Confirm deferred MPI/CUDA-or-alternative packages/runtimes are absent, and
  that no OpenMP or sanitizer runtime ships unless its exact implementation has
  completed the conditional register review.
- [ ] For any development container/SDK that intentionally includes those tools,
  open a separate distribution inventory and satisfy their source/notice terms.
- [ ] Confirm HighFive, RapidXML, direct Armadillo/FreeType/GLFW/OpenGL, vcpkg Qt
  packages and the historical external mathplot checkout are absent.

Evidence: package inventory, link graph, process/file-boundary statement and
feature record. Owners: component owners, `SWE-REL-PKG`, `SWE-ENV-LIC`.

### 7. GPL, LGPL, MPL and proprietary review

- [ ] Review every GPL/LGPL/MPL/custom/proprietary entry and document why the
  actual use/distribution conforms.
- [ ] Verify Qt module-level LGPL/GPL status rather than applying one licence label
  to all Qt components.
- [ ] Verify Eigen MPL-covered files and source/notice availability.
- [ ] Verify GPL tools/applications are external, or satisfy full distribution
  obligations in a separately approved bundle.
- [ ] Verify no MATLAB, MSVC, Qt commercial or other proprietary artefact is
  redistributed outside its applicable agreement/redistributable terms.
- [ ] Escalate any ambiguous linking, plugin, modified-source, app-store/DRM or
  combined-work question for legal review before release.

Evidence: component decision notes and any controlled legal approvals. Owner:
`SWE-ENV-LIC`; `SWE-REL-PKG` enforces the gate.

### 8. Dataset and geospatial-resource licences

- [ ] Keep scientific dataset licences separate from software dependency notices
  and link each packaged dataset to its source, version, terms and attribution.
- [ ] Inventory `proj.db`, PROJ grids and other geospatial resources actually
  packaged.
- [ ] For every PROJ-data grid, verify its individual entry in
  `copyright_and_licenses.csv`/source metadata and apply attribution or
  share-alike requirements. Never label the whole collection MIT.
- [ ] Verify GDAL driver data, EPSG/resource databases, sample fixtures, maps,
  fonts and imagery separately when present.
- [ ] Exclude any dataset/resource whose licence, provenance, consent, access
  condition or redistribution right is not recorded.

Evidence: dataset/resource manifest, copied terms, attribution and provenance.
Owners: `SWE-GEO-CRS`, `SWE-DAT-MAN`/relevant data owner, `SWE-ENV-LIC`.

### 9. Notices, licence texts and source actions

- [ ] Build the third-party notice index from the verified component inventory,
  including header-only and statically linked components.
- [ ] Include exact required licence texts, copyright notices, disclaimers and
  attributions without removing upstream statements.
- [ ] Include LGPL/MPL/GPL corresponding-source or offer/instruction material and
  installation/relinking information where required.
- [ ] Make notices accessible from the installed application/package and retain
  them beside downloadable archives/installers.
- [ ] Diff the notice bundle against the previous release and explain additions,
  removals and text changes.
- [ ] Verify links in compliance material are durable supplements, not substitutes
  for licence text/source actions that must accompany distribution.

Evidence: final notice bundle, source archive/offer material and diff. Owner:
`SWE-ENV-LIC`; `SWE-REL-PKG` packages and tests accessibility.

### 10. Evidence review and approval

- [ ] Each component owner has confirmed role, version, feature/use mode and
  external-versus-packaged decision.
- [ ] `SWE-ENV-DEP` has confirmed the exact manifest/registry graph.
- [ ] `SWE-ENV-LIC` has reconciled every inventory item to an identified licence,
  notice/source action and redistribution decision.
- [ ] `SWE-REL-PKG` has confirmed that the reviewed tree is byte-for-byte the
  candidate being published.
- [ ] The approver has recorded decision, name/role, date and evidence links.
- [ ] All exceptions have an owner, written approval and expiry/re-review trigger;
  there are no informal waivers.

## Mandatory stop conditions

Do not publish when any of the following is true:

- a direct or transitive dependency differs from the reviewed graph;
- installed copyright metadata is missing or incomplete;
- a runtime/plugin/resource/dataset has no register mapping;
- the actual Qt module set, linkage or licence route is unknown;
- GPL/LGPL/MPL/custom/proprietary obligations are unresolved;
- an external tool is bundled without a separate redistribution decision;
- required notices, licence texts, source material or attributions are absent;
- evidence is not durable and traceable to the exact package digest; or
- approval responsibility has not been discharged.

The only acceptable outcome for an unclassified component is to remove it from
the candidate or complete classification and repeat the affected checks. A risk
note alone does not pass the gate.

## Approval record template

```text
Candidate digest reviewed:
Dependency graph evidence:
Installed copyright evidence:
Qt SBOM/linkage evidence:
Runtime/package inventory:
Dataset/resource evidence:
Final notice/source bundle:
Exceptions (or "none"):

Prepared by / date:
Component-owner attestations:
SWE-ENV-LIC reviewer / date:
SWE-REL-PKG approver / date:
Decision: APPROVED | REJECTED
```
