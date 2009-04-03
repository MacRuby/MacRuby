assert '0..42', "r = 0..42; p r"
assert '0..42', "b=0; e=42; r = b..e; p r"
assert 'false', "r = 0..42; p r.exclude_end?"
assert 'false', "b=0; e=42; r = b..e; p r.exclude_end?"

assert '0...42', "r = 0...42; p r"
assert '0...42', "b=0; e=42; r = b...e; p r"
assert 'true',   "r = 0...42; p r.exclude_end?"
assert 'true',   "b=0; e=42; r = b...e; p r.exclude_end?"
