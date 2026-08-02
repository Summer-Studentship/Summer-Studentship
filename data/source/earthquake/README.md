# Earthquake Source Inputs

The Tohoku finite-fault source for the G3 earthquake-coupling artifact is intentionally not downloaded by tests, CMake, or normal runtime paths.

Manual acquisition command:

```sh
mkdir -p data/source/earthquake
curl -L \
  https://earthquake.usgs.gov/archive/product/finite-fault/usp000hvnu/us/1539808472261/basic_inversion.param \
  -o data/source/earthquake/usgs_usp000hvnu_1539808472261_basic_inversion.param
```

The artifact producer expects that file at:

```text
data/source/earthquake/usgs_usp000hvnu_1539808472261_basic_inversion.param
```

Install the preprocessing-only dependencies from `tools/earthquake/requirements-tohoku-artifact.txt` before producing GeoTIFF artifacts.
