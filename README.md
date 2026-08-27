# A Software ISP in C++. Takes raw Bayer in and output image.

Dev and tested with RAW10 Bayer frame off a Sony IMX219 and produces a viewable colour image. 
No 3rd party stuffs (except some calibrated numbers from imx219 :P).

Frames might come from pi_caputre project on a Raspberry Pi.

## The pipeline

```
unpack RAW10 -> black level -> white balance -> demosaic -> colour matrix -> gamma -> image
```

| stage | what it does |
|---|---|
| **RAW10 unpack** | `SRGGB10P` packs 4 pixels into 5 bytes, need to unpack |
| **Black level** | The IMX sensor's ADC never reads 0 for black. Subtract the per-phase pedestal, measured from a sample capped-lens dark frame |
| **White balance** | Grey-world: scale R and B so the scene averages neutral. Use greens as reference (gain=1.0) |
| **Demosaic** | Each photosite sees one colour through a Bayer filter. Bilinear interpolation fills the other two |
| **Colour matrix** | 3×3 correction for filter crosstalk, interpolated between six calibrated illuminants from imx219 software |
| **Gamma** | 2.2 power law, then clamp to 8-bit. |

## Build and run

```bash
cmake -S . -B build && cmake --build build
(ctest --test-dir ./build --output-on-failure) # 10 tests, optional

# first 3 arguments are positional, sorry....
./build/isp --packed packed000.raw out.pgm --dark black_packed000.raw
```

On a Pi with the [capture program](../pi_capture/) next door, `capture_and_process.sh`
does both halves in one command — capture a fresh frame, then develop it.

It will write 8 stage previews to `build/pipeline_out/`, then prints some stats about what it has computed, example:
```
black level: R=64 Gr=64 Gb=64 B=64
wb gains: R=1.6313 Gr=1.0000 Gb=1.0000 B=1.6342
white point: 959
illuminant : 5858.0000 K (selected)
color temperature estimate: r/g=5930.2157(K), b/g=4316.8744(K)
```

| flag | |
|---|---|
| `--packed` / `--unpacked` | `SBGGR10P` (4px/5B) or `SBGGR10` (16-bit LE) |
| `--dark <file>` | dark frame for the black-level measurement |
| `--stage <name>` | early stop after `decode`, `black_level`, `white_balance`, `demosaic`, `ccm`, or `gamma` |
| `--no-dump` | skip the per-stage previews |
| `--illuminant <K>` | colour temperature for the colour matrix (default 5858) |

## Requirements
Requires a C++20 compiler and CMake ≥ 3.18. Tested and builds clean on GCC 10.2 and Clang 19.

## Testing
Not exactly 100%, the goldens for the tests were produced from a build that looked "fairly good".
Those were saved, then used for refactoring (code was too "researchy").
Some manual hand-computation and calculation were also less manual, `tools/isp_compare.cpp` was 
developed and used heavily during refactoring to make sure no implementation deviate.
`tools/isp_compare.cpp` reports differing sample count, max and mean delta, a per-channel breakdown,
and the first differing position.

**The goldens structurally cannot verify some stuffs** (`tools/test_bayer_phases.cpp`):
- Bayer phase indexing and mapping across different orderings, synthesize the different inputs
  and to make sure logic is right.
- round-tripping packing/unpacking should provide 0 differences.
- the **high-byte invariant**: read `SBGGR10P` should have exact same high 8-bits as against a real capture without
  any unpacking.
- partial-group rejection, make sure no-one allows this. partial-group fault-tolerancy not sure how.

## Future stuffs

- Lens shading correction.
- Self-calibrated colour matrix. This breaks my "no 3rd party" :(
- A better demosaic?
