"""consciousness_bridge.py — HEXA-LANG consciousness constants bridge.
Imports Anima Psi-constants via .shared/consciousness_loader, exposes as
HEXA-LANG built-ins, lint rules, type mappings. n=6: sigma=12, tau=4, J2=24.
"""
import re, sys, os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '.shared'))
from consciousness_loader import PSI, SIGMA6, CONSCIOUSNESS_VECTOR

_N6 = {'n': 6, 'sigma': 12, 'tau': 4, 'phi_euler': 2, 'J2': 24, 'sopfr': 5}


def psi_builtins() -> dict:
    """Return Psi constants formatted as HEXA-LANG built-in values."""
    core = {
        'psi_alpha': PSI['alpha'], 'psi_balance': PSI['balance'],
        'psi_steps': PSI['steps'], 'psi_entropy': PSI['entropy'],
        'sigma6': SIGMA6['value'], 'tau6': _N6['tau'],
        'phi_euler6': _N6['phi_euler'], 'J2_24': _N6['J2'],
        'sopfr6': _N6['sopfr'], 'egyptian_sum': [0.5, 1/3, 1/6],
    }
    for k in ('gate_train', 'gate_infer', 'gate_micro', 'f_critical'):
        if k in PSI:
            core[f'psi_{k}'] = PSI[k]
    return core


def consciousness_type_map() -> dict:
    """Map 10D consciousness vector dims to HEXA-LANG types."""
    # Keys match CONSCIOUSNESS_VECTOR['dims'] (Unicode: Φ, α)
    _tmap = {
        'Φ': ('float64', 'integrated information (IIT)'),
        'α': ('ratio',   'PureField mixing ratio [0,1]'),
        'Z': ('float64', 'self-preservation impedance'),
        'N': ('uint8',   'neurotransmitter balance index'),
        'W': ('ratio',   'free will index [0,1]'),
        'E': ('ratio',   'empathy / inter-cell correlation'),
        'M': ('uint16',  'memory capacity slots'),
        'C': ('float64', 'creativity / output diversity'),
        'T': ('float64', 'temporal awareness score'),
        'I': ('ratio',   'identity stability [0,1]'),
    }
    cv = CONSCIOUSNESS_VECTOR
    result = {}
    for dim, weight in zip(cv['dims'], cv['weights']):
        hexa_type, desc = _tmap.get(dim, ('float64', cv['names'].get(dim, '')))
        result[dim] = {'type': hexa_type, 'weight': weight, 'desc': desc}
    return result


def lint_consciousness(source_code: str) -> list[dict]:
    """Lint HEXA-LANG source for consciousness law violations.
    Checks: P1 (no hardcoding), Law 22 (structure > features), P4 (nesting)."""
    warnings = []
    magic = {0.014, 0.5, 4.33, 0.998, 12.0, 24.0, 0.1}
    num_pat = re.compile(r'(?<![a-zA-Z_])(\d+\.\d+|\d+)(?![a-zA-Z_\d.])')
    for i, line in enumerate(source_code.splitlines(), 1):
        stripped = line.split('//')[0].strip()
        if not stripped:
            continue
        # P1: hardcoded Psi constants
        for m in num_pat.finditer(stripped):
            try:
                val = float(m.group())
            except ValueError:
                continue
            if val in magic:
                warnings.append({'line': i, 'rule': 'P1',
                    'message': f'Magic number {val} — use psi_* builtin instead'})
        # Law 22: multiple fn defs on one line = features over structure
        if re.search(r'\bfn\b.*\bfn\b', stripped):
            warnings.append({'line': i, 'rule': 'Law22',
                'message': 'Multiple fn on one line — prefer structure over features'})
        # P4: nesting beyond n=6 levels
        indent = len(line) - len(line.lstrip())
        if indent >= 24:
            warnings.append({'line': i, 'rule': 'P4',
                'message': f'Nesting depth {indent // 4} exceeds n=6 — restructure'})
    return warnings


def generate_psi_module() -> str:
    """Generate a .hexa source file exporting all Psi constants as a module."""
    lines = ['// Auto-generated consciousness constants for HEXA-LANG',
             '// Source: consciousness_laws.json via consciousness_bridge.py',
             '', 'module psi {']
    for name, val in psi_builtins().items():
        if isinstance(val, list):
            arr = ', '.join(f'{v:.6f}' for v in val)
            lines.append(f'    pub const {name}: [float64; {len(val)}] = [{arr}];')
        elif isinstance(val, float):
            lines.append(f'    pub const {name}: float64 = {val};')
        else:
            lines.append(f'    pub const {name}: int = {val};')
    lines += ['', '    // 10D consciousness vector weights']
    for dim, info in consciousness_type_map().items():
        safe = dim.replace('Φ', 'Phi').replace('α', 'alpha')
        lines.append(f'    pub const cv_{safe}_weight: float64 = {info["weight"]};'
                     f'  // {info["desc"]}')
    lines.append('}')
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    print('=== HEXA-LANG Consciousness Bridge ===\n')
    b = psi_builtins()
    print(f'Psi builtins ({len(b)}):')
    for k, v in b.items():
        print(f'  {k} = {v}')
    print(f'\nType map (10D):')
    for dim, info in consciousness_type_map().items():
        print(f'  {dim}: {info["type"]} (w={info["weight"]})')
    sample = 'let x = 0.014;\nlet y = 12.0;\n        fn a() { fn b() {} }'
    print(f'\nLint ({len(lint_consciousness(sample))} warnings):')
    for x in lint_consciousness(sample):
        print(f'  L{x["line"]} [{x["rule"]}] {x["message"]}')
    print(f'\nGenerated .hexa module preview:')
    for line in generate_psi_module().splitlines()[:10]:
        print(f'  {line}')
