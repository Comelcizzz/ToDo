# Official metering validation

Package: EBU Loudness Test Set v05 (Tech 3341 / 3342)

Scope: mono/stereo only. Multichannel out of scope.

True Peak: Variant A (official Tech 3341 cases 15–23 PASS).

| Vector ID | Metric | Expected | Actual | Delta | Tolerance | Result |
|---|---|---:|---:|---:|---:|---|
| ebu3341-1 | integratedLufs | -23 | -22.9968 | 0.003168 | 0.100000 | PASS |
| ebu3341-2 | integratedLufs | -33 | -33.0031 | -0.00313605 | 0.100000 | PASS |
| ebu3341-3 | integratedLufs | -23 | -23.0574 | -0.0574188 | 0.100000 | PASS |
| ebu3341-4 | integratedLufs | -23 | -23.0574 | -0.0574188 | 0.100000 | PASS |
| ebu3341-5 | integratedLufs | -23 | -23.0223 | -0.0223063 | 0.100000 | PASS |
| ebu3341-6 | integratedLufs | -23 | 0 | 0 | 0.100000 | SKIP |
| ebu3341-7 | integratedLufs | -23 | -23.0355 | -0.0354873 | 0.100000 | PASS |
| ebu3341-8 | integratedLufs | -23 | -23.0406 | -0.0406283 | 0.100000 | PASS |
| ebu3341-9 | shortTermLufs | -23 | -23.0295 | -0.0295449 | 0.100000 | PASS |
| ebu3341-12 | momentaryLufs | -23 | -23.0037 | -0.00372137 | 0.100000 | PASS |
| ebu3341-15 | truePeakDbtp | -6 | -6.00026 | -0.000264939 | +0.200000/-0.400000 | PASS |
| ebu3341-16 | truePeakDbtp | -6 | -5.99696 | 0.00304356 | +0.200000/-0.400000 | PASS |
| ebu3341-17 | truePeakDbtp | -6 | -5.99924 | 0.000756917 | +0.200000/-0.400000 | PASS |
| ebu3341-18 | truePeakDbtp | -6 | -6.00254 | -0.00254168 | +0.200000/-0.400000 | PASS |
| ebu3341-19 | truePeakDbtp | 3 | 3.01303 | 0.0130266 | +0.200000/-0.400000 | PASS |
| ebu3341-20 | truePeakDbtp | 0 | -0.130183 | -0.130183 | +0.200000/-0.400000 | PASS |
| ebu3341-21 | truePeakDbtp | 0 | -0.11874 | -0.11874 | +0.200000/-0.400000 | PASS |
| ebu3341-22 | truePeakDbtp | 0 | -0.136522 | -0.136522 | +0.200000/-0.400000 | PASS |
| ebu3341-23 | truePeakDbtp | 0 | -0.118738 | -0.118738 | +0.200000/-0.400000 | PASS |
| ebu3342-1 | loudnessRangeLu | 10 | 10.0011 | 0.00110549 | 1.000000 | PASS |
| ebu3342-2 | loudnessRangeLu | 5 | 4.99937 | -0.000626595 | 1.000000 | PASS |
| ebu3342-3 | loudnessRangeLu | 20 | 19.9951 | -0.00493593 | 1.000000 | PASS |
| ebu3342-4 | loudnessRangeLu | 15 | 14.9993 | -0.000726062 | 1.000000 | PASS |
| ebu3342-5 | loudnessRangeLu | 5 | 4.97662 | -0.0233842 | 1.000000 | PASS |
| ebu3342-6 | loudnessRangeLu | 15 | 15.0111 | 0.0111349 | 1.000000 | PASS |

Summary: present=24 missing=0 skipped=1 passed=24 failed=0
