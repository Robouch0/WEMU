"""Tiny builder for Latte (R7xx) shader assembly in the AMD disassembly dialect
accepted by decaf-emu's latte-assembler.

The output keeps the structure the Wii U's offline compiler produces: control
flow with ALU/TEX clauses, VLIW groups across the x/y/z/w/t units, uniform
blocks locked through KCACHE, and GFD metadata in `; $KEY = value` comments.
Groups are written by hand (no scheduler), so they are denser than a naive
one-op-per-group form but less packed than shipped game shaders.
"""

MAX_ALU_SLOTS = 128
_FLAGS = ('CLAMP', 'UPDATE_EXEC_MASK', 'UPDATE_PRED')
MAX_TEX_FETCHES = 16
_UNITS = 'xyzw'


def _component(operand, unit_index):
    """Selects one component of a 4-component swizzled operand for a vector unit."""
    if operand.endswith('f') or operand.startswith('0x') or operand.startswith('('):
        return operand
    prefix, abs_wrap = '', False
    if operand.startswith('-'):
        prefix, operand = '-', operand[1:]
    if operand.startswith('|'):
        abs_wrap, operand = True, operand.strip('|')
    base, _, swizzle = operand.rpartition('.')
    if not base or len(swizzle) != 4:
        raise ValueError(f'vector operand needs a 4-component swizzle: {operand}')
    channel = swizzle[unit_index]
    if channel == '0':
        return '0.0f'
    if channel == '1':
        return '1.0f'
    value = f'{base}.{channel}'
    if abs_wrap:
        value = f'|{value}|'
    return prefix + value


def _literal_count(sources):
    return sum(1 for s in sources if s.lstrip('-|').endswith('f') and s.lstrip('-|') not in ('0.0f', '1.0f', '0.5f'))


class AluClause:
    def __init__(self, shader, kcache0=None, kcache1=None, kind='ALU'):
        self.shader, self.kind = shader, kind
        self.kcache0, self.kcache1 = kcache0, kcache1
        self.groups = []

    def group(self, *slots):
        """slots: (unit, opcode, dst, *sources[, 'CLAMP']). Returns the group id for PV/PS."""
        units = [slot[0] for slot in slots]
        if len(set(units)) != len(units):
            raise ValueError(f'unit used twice in one group: {units}')
        for unit, _, dst, *_ in slots:
            if unit != 't' and dst != '____' and not dst.endswith('.' + unit):
                raise ValueError(f'vector unit {unit} must write channel {unit}: {dst}')
        gid = self.shader._next_id()
        self.groups.append((gid, slots))
        return gid

    def vec(self, opcode, dst, *sources, mask='xyzw', clamp=False, trans=None):
        """One opcode across the channels in `mask`; `trans` adds a t-unit slot."""
        slots = []
        for unit in mask:
            index = _UNITS.index(unit)
            slot = (unit, opcode, f'{dst}.{unit}', *[_component(s, index) for s in sources])
            slots.append(slot + (('CLAMP',) if clamp else ()))
        if trans:
            slots.append(('t',) + tuple(trans))
        return self.group(*slots)

    def dot4(self, dst, a, b, trans=None):
        """DOT4 over x/y/z/w; `dst` like 'R5.y' is written by its own unit."""
        reg, _, channel = dst.rpartition('.')
        slots = []
        for index, unit in enumerate(_UNITS):
            target = dst if unit == channel else '____'
            slots.append((unit, 'DOT4', target, _component(a, index), _component(b, index)))
        if trans:
            slots.append(('t',) + tuple(trans))
        return self.group(*slots)

    def trans(self, opcode, dst, *sources):
        return self.group(('t', opcode, dst, *sources))

    def slot_count(self):
        count = 0
        for _, slots in self.groups:
            literals = sum(_literal_count(slot[3:]) for slot in slots)
            count += len(slots) + (literals + 1) // 2
        return count

    def render(self, number):
        props = []
        if self.kcache0:
            props.append('KCACHE0(CB{}:{}-{})'.format(*self.kcache0))
        if self.kcache1:
            props.append('KCACHE1(CB{}:{}-{})'.format(*self.kcache1))
        lines = [f'{number:02d} {self.kind}: {" ".join(props)}'.rstrip()]
        for gid, slots in self.groups:
            for index, (unit, opcode, dst, *sources) in enumerate(slots):
                flags = [s for s in sources if s in _FLAGS]
                sources = [s for s in sources if s not in _FLAGS]
                head = f'{gid:>7}  ' if index == 0 else ' ' * 9
                operands = ', '.join([dst] + sources)
                lines.append(f'{head}{unit}: {opcode:<12}{operands}{" " + " ".join(flags) if flags else ""}')
        return lines


class TexClause:
    def __init__(self, shader):
        self.shader, self.fetches = shader, []

    def fetch(self, text):
        self.fetches.append((self.shader._next_id(), text))

    def render(self, number):
        lines = [f'{number:02d} TEX: VALID_PIX' if self.shader.kind == 'ps' else f'{number:02d} TEX:']
        lines += [f'{gid:>7}  {text}' for gid, text in self.fetches]
        return lines


class Shader:
    def __init__(self, kind, name):
        if kind not in ('vs', 'ps'):
            raise ValueError(kind)
        self.kind, self.name = kind, name
        self.meta, self.cf, self._id = ['; $MODE = "UniformBlock"'], [], 0

    def _next_id(self):
        gid = self._id
        self._id += 1
        return gid

    def set(self, key, value):
        if isinstance(value, bool):
            value = 'true' if value else 'false'
        elif isinstance(value, str):
            value = f'"{value}"'
        self.meta.append(f'; ${key} = {value}')

    def uniform_block(self, index, name, size):
        self.set(f'UNIFORM_BLOCKS[{index}].name', name)
        self.set(f'UNIFORM_BLOCKS[{index}].offset', index)
        self.set(f'UNIFORM_BLOCKS[{index}].size', size)

    def attribs(self, *attribs):
        """(name, type) in GPR order: attribute i lands in R(i+1) via the fetch shader."""
        for index, (name, kind) in enumerate(attribs):
            self.set(f'ATTRIB_VARS[{index}].name', name)
            self.set(f'ATTRIB_VARS[{index}].type', kind)
            self.set(f'ATTRIB_VARS[{index}].location', index)

    def vs_outputs(self, count):
        """Semantic i for PARAM i; the pixel shader receives them in R0..R(count-1)."""
        self.set('SPI_VS_OUT_CONFIG.VS_EXPORT_COUNT', max(count - 1, 0))
        registers = (count + 3) // 4
        self.set('NUM_SPI_VS_OUT_ID', registers)
        for index in range(count):
            self.set(f'SPI_VS_OUT_ID[{index // 4}].SEMANTIC_{index % 4}', index)

    def ps_inputs(self, count):
        self.set('NUM_SPI_PS_INPUT_CNTL', count)
        for index in range(count):
            self.set(f'SPI_PS_INPUT_CNTL[{index}].SEMANTIC', index)
            self.set(f'SPI_PS_INPUT_CNTL[{index}].DEFAULT_VAL', 1)

    def samplers(self, *samplers):
        for index, (name, kind) in enumerate(samplers):
            self.set(f'SAMPLER_VARS[{index}].name', name)
            self.set(f'SAMPLER_VARS[{index}].type', kind)
            self.set(f'SAMPLER_VARS[{index}].location', index)

    def call_fs(self):
        self.cf.append('CALL_FS NO_BARRIER')

    def alu(self, kcache0=None, kcache1=None, kind='ALU'):
        """kind: ALU, ALU_PUSH_BEFORE, ALU_POP_AFTER, ALU_BREAK, ..."""
        clause = AluClause(self, kcache0, kcache1, kind)
        self.cf.append(clause)
        return clause

    def next_cf(self):
        """Index the next control-flow instruction will get (for jump targets)."""
        return len(self.cf)

    def cf_inst(self, text):
        """Raw control-flow instruction, e.g. 'JUMP POP_CNT(1) ADDR(8)'; returns its index."""
        self.cf.append(text)
        return len(self.cf) - 1

    def tex(self):
        clause = TexClause(self)
        self.cf.append(clause)
        return clause

    def export(self, target, source, done=False):
        self.cf.append(f'{"EXP_DONE" if done else "EXP"}: {target}, {source}' + ('' if target.startswith('POS') else ' NO_BARRIER'))

    def render(self):
        lines = list(self.meta) + ['']
        for number, item in enumerate(self.cf):
            if isinstance(item, AluClause):
                if item.slot_count() > MAX_ALU_SLOTS:
                    raise ValueError(f'{self.name}: ALU clause {number} has {item.slot_count()} slots (max {MAX_ALU_SLOTS})')
                lines += item.render(number)
            elif isinstance(item, TexClause):
                if len(item.fetches) > MAX_TEX_FETCHES:
                    raise ValueError(f'{self.name}: TEX clause {number} has {len(item.fetches)} fetches')
                lines += item.render(number)
            else:
                lines.append(f'{number:02d} {item}')
        lines.append('END_OF_PROGRAM')
        return '\n'.join(lines) + '\n'
