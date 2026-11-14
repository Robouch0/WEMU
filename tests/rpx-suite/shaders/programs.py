#!/usr/bin/env python3
"""Generates the suite's Latte shader programs as .vsh/.psh pairs.

Each program becomes one GFD (.gsh) file holding one vertex and one pixel
shader. Uniform-block layouts here must match src/shaders.h.

Usage: programs.py OUT_DIR
"""

import sys
from pathlib import Path

from latte import Shader

SUN_LIGHTS = 8  # lights evaluated per pixel by the lit program


def splat(component):
    """'KC0[1].z' -> 'KC0[1].zzzz'"""
    base, _, channel = component.rpartition('.')
    return f'{base}.{channel * 4}'


def mvp_rows(clause, dst, src, bank, first_row):
    for row, channel in enumerate('xyzw'):
        clause.dot4(f'{dst}.{channel}', f'{src}.xyzw', f'{bank}[{first_row + row}].xyzw')


# ---------------------------------------------------------------------------
# lit: textured, 8 per-pixel point lights (Blinn-Phong), 4-tap PCF shadow from light 0
# VS UB0 Object: [0-3] MVP rows, [4-6] model rows, [7] tint
# VS UB1 Frame:  [0-3] light view-projection rows
# PS UB0 Lighting: [0] ambient, [1] camera, [2] shadow (u scale, v scale, bias, strength),
#                  [3] shadow texel, [4+2i] light pos + 1/r^2, [5+2i] light colour + specular
def lit():
    vs = Shader('vs', 'lit')
    vs.uniform_block(0, 'Object', 256)
    vs.uniform_block(1, 'Frame', 256)
    vs.attribs(('aPosition', 'vec3'), ('aNormal', 'vec3'), ('aTexCoord', 'vec2'))
    vs.vs_outputs(5)
    vs.call_fs()
    a = vs.alu(kcache0=(0, 0, 15), kcache1=(1, 0, 15))
    mvp_rows(a, 'R4', 'R1', 'KC0', 0)
    a.dot4('R5.x', 'R1.xyzw', 'KC0[4].xyzw')
    a.dot4('R5.y', 'R1.xyzw', 'KC0[5].xyzw')
    a.dot4('R5.z', 'R1.xyzw', 'KC0[6].xyzw', trans=('MOV', 'R5.w', '1.0f'))
    a.dot4('R6.x', 'R2.xyz0', 'KC0[4].xyzw')
    a.dot4('R6.y', 'R2.xyz0', 'KC0[5].xyzw')
    a.dot4('R6.z', 'R2.xyz0', 'KC0[6].xyzw', trans=('MOV', 'R6.w', '0.0f'))
    mvp_rows(a, 'R7', 'R5', 'KC1', 0)
    a.vec('MOV', 'R8', 'KC0[7].xyzw')
    vs.export('POS0', 'R4.xyzw', done=True)
    vs.export('PARAM0', 'R5.xyzw')
    vs.export('PARAM1', 'R6.xyzw')
    vs.export('PARAM2', 'R3.xy00')
    vs.export('PARAM3', 'R7.xyzw')
    vs.export('PARAM4', 'R8.xyzw', done=True)

    ps = Shader('ps', 'lit')
    ps.uniform_block(0, 'Lighting', 512)
    # Declared sampler2D: the assembler has no shadow sampler type; SAMPLE_C + the
    # sampler's compare function do the comparison.
    ps.samplers(('uAlbedo', 'sampler2D'), ('uShadow', 'sampler2D'))
    ps.ps_inputs(5)  # R0 world pos, R1 normal, R2 uv, R3 shadow clip, R4 tint
    t = ps.tex()
    t.fetch('SAMPLE R5.xyzw, R2.xy00, t0, s0')
    a = ps.alu(kcache0=(0, 0, 31))
    a.dot4('R6.x', 'R1.xyz0', 'R1.xyz0', trans=('RECIP_IEEE', 'R6.w', 'R3.w'))
    rsq = a.trans('RECIPSQRT_IEEE', 'R6.y', 'R6.x')
    a.vec('MUL', 'R1', 'R1.xyzw', f'PS{rsq}.xxxx', mask='xyz')
    a.vec('ADD', 'R7', 'KC0[1].xyzw', '-R0.xyzw', mask='xyz')
    a.dot4('R6.z', 'R7.xyz0', 'R7.xyz0')
    a.trans('RECIPSQRT_IEEE', 'R6.z', 'R6.z')
    a.vec('MUL', 'R7', 'R7.xyzw', 'R6.zzzz', mask='xyz')
    a.vec('MUL', 'R8', 'R3.xyzw', 'R6.wwww', mask='xyz')
    a.group(('x', 'MULADD', 'R8.x', 'R8.x', 'KC0[2].x', '0.5f'),
            ('y', 'MULADD', 'R8.y', 'R8.y', 'KC0[2].y', '0.5f'),
            ('z', 'MULADD', 'R8.z', 'R8.z', '0.5f', '0.5f'))
    a.group(('w', 'ADD', 'R8.w', 'R8.z', '-KC0[2].z'))
    for reg, (sx, sy) in zip(('R9', 'R10', 'R11', 'R12'), ((-1, -1), (1, -1), (-1, 1), (1, 1))):
        a.group(('x', 'ADD', f'{reg}.x', 'R8.x', ('' if sx > 0 else '-') + 'KC0[3].x'),
                ('y', 'ADD', f'{reg}.y', 'R8.y', ('' if sy > 0 else '-') + 'KC0[3].x'),
                ('w', 'MOV', f'{reg}.w', 'R8.w'))
    t = ps.tex()
    t.fetch('SAMPLE_C R13.x___, R9.xy0w, t1, s1')
    t.fetch('SAMPLE_C R13._x__, R10.xy0w, t1, s1')
    t.fetch('SAMPLE_C R13.__x_, R11.xy0w, t1, s1')
    t.fetch('SAMPLE_C R13.___x, R12.xy0w, t1, s1')
    a = ps.alu(kcache0=(0, 0, 31))
    a.dot4('R14.x', 'R13.xyzw', '0.25f')
    a.group(('x', 'ADD', 'R14.x', 'R14.x', '-1.0f'))
    a.group(('x', 'MULADD', 'R12.x', 'R14.x', 'KC0[2].w', '1.0f'))
    a.vec('MOV', 'R14', 'KC0[0].xyzw', mask='xyz')
    a.vec('MOV', 'R15', '0.0f', mask='xyz')
    for light in range(SUN_LIGHTS):
        base = 4 + 2 * light
        a = ps.alu(kcache0=(0, 0, 31))
        a.vec('ADD', 'R16', f'KC0[{base}].xyzw', '-R0.xyzw', mask='xyz')
        d2 = a.dot4('R16.w', 'R16.xyz0', 'R16.xyz0')
        inv = a.group(('z', 'MULADD', 'R17.z', f'-PV{d2}.w', f'KC0[{base}].w', '1.0f', 'CLAMP'),
                      ('t', 'RECIPSQRT_IEEE', 'R17.x', f'PV{d2}.w'))
        a.vec('MUL', 'R16', 'R16.xyzw', f'PS{inv}.xxxx', mask='xyz')
        a.vec('ADD', 'R18', 'R16.xyzw', 'R7.xyzw', mask='xyz')
        a.dot4('R17.y', 'R1.xyz0', 'R16.xyz0')
        a.dot4('R18.w', 'R18.xyz0', 'R18.xyz0')
        a.group(('y', 'MAX', 'R17.y', 'R17.y', '0.0f'), ('t', 'RECIPSQRT_IEEE', 'R17.w', 'R18.w'))
        a.vec('MUL', 'R18', 'R18.xyzw', 'R17.wwww', mask='xyz')
        a.dot4('R19.x', 'R1.xyz0', 'R18.xyz0')
        a.group(('x', 'MAX', 'R19.x', 'R19.x', '0.0f'), ('y', 'MUL', 'R17.y', 'R17.y', 'R17.z'))
        if light == 0:
            a.group(('y', 'MUL', 'R17.y', 'R17.y', 'R12.x'))
        for _ in range(5):  # specular power 32
            a.group(('x', 'MUL', 'R19.x', 'R19.x', 'R19.x'))
        a.group(('x', 'MUL', 'R19.x', 'R19.x', 'R17.z'))
        a.group(('x', 'MUL', 'R19.x', 'R19.x', f'KC0[{base + 1}].w'))
        if light == 0:
            a.group(('x', 'MUL', 'R19.x', 'R19.x', 'R12.x'))
        a.vec('MULADD', 'R14', f'KC0[{base + 1}].xyzw', 'R17.yyyy', 'R14.xyzw', mask='xyz')
        a.vec('MULADD', 'R15', f'KC0[{base + 1}].xyzw', 'R19.xxxx', 'R15.xyzw', mask='xyz')
    a = ps.alu()
    a.vec('MUL', 'R20', 'R5.xyzw', 'R4.xyzw', mask='xyz')
    a.vec('MULADD', 'R20', 'R20.xyzw', 'R14.xyzw', 'R15.xyzw', mask='xyz', trans=('MOV', 'R20.w', '1.0f'))
    ps.export('PIX0', 'R20.xyzw', done=True)
    return vs, ps


# lit_instanced: the lit pixel shader fed by hardware instancing. Per-instance
# model rows and tint come from attribute buffer 1 (divisor 1).
# VS UB0 Camera: [0-3] view-projection rows; UB1 Frame: [0-3] light view-projection rows
def lit_instanced():
    vs = Shader('vs', 'lit_instanced')
    vs.uniform_block(0, 'Camera', 256)
    vs.uniform_block(1, 'Frame', 256)
    vs.attribs(('aPosition', 'vec3'), ('aNormal', 'vec3'), ('aTexCoord', 'vec2'),
               ('aRow0', 'vec4'), ('aRow1', 'vec4'), ('aRow2', 'vec4'), ('aTint', 'vec4'))
    vs.vs_outputs(5)
    vs.call_fs()
    a = vs.alu(kcache0=(0, 0, 15), kcache1=(1, 0, 15))
    a.dot4('R8.x', 'R1.xyzw', 'R4.xyzw')
    a.dot4('R8.y', 'R1.xyzw', 'R5.xyzw')
    a.dot4('R8.z', 'R1.xyzw', 'R6.xyzw', trans=('MOV', 'R8.w', '1.0f'))
    a.dot4('R9.x', 'R2.xyz0', 'R4.xyzw')
    a.dot4('R9.y', 'R2.xyz0', 'R5.xyzw')
    a.dot4('R9.z', 'R2.xyz0', 'R6.xyzw', trans=('MOV', 'R9.w', '0.0f'))
    mvp_rows(a, 'R10', 'R8', 'KC0', 0)
    mvp_rows(a, 'R11', 'R8', 'KC1', 0)
    vs.export('POS0', 'R10.xyzw', done=True)
    vs.export('PARAM0', 'R8.xyzw')
    vs.export('PARAM1', 'R9.xyzw')
    vs.export('PARAM2', 'R3.xy00')
    vs.export('PARAM3', 'R11.xyzw')
    vs.export('PARAM4', 'R7.xyzw', done=True)
    return vs, lit()[1]


# depth: position-only pass used for shadow maps. VS UB0 Object: [0-3] MVP rows.
def depth():
    vs = Shader('vs', 'depth')
    vs.uniform_block(0, 'Object', 256)
    vs.attribs(('aPosition', 'vec3'), ('aNormal', 'vec3'), ('aTexCoord', 'vec2'))
    vs.vs_outputs(1)
    vs.call_fs()
    a = vs.alu(kcache0=(0, 0, 15))
    mvp_rows(a, 'R4', 'R1', 'KC0', 0)
    vs.export('POS0', 'R4.xyzw', done=True)
    vs.export('PARAM0', 'R3.xy00', done=True)

    ps = Shader('ps', 'depth')
    ps.ps_inputs(1)
    a = ps.alu()
    a.vec('MOV', 'R1', 'R0.xy01')
    ps.export('PIX0', 'R1.xyzw', done=True)
    return vs, ps


def fullscreen_vs(name):
    vs = Shader('vs', name)
    vs.attribs(('aPosition', 'vec2'), ('aTexCoord', 'vec2'))
    vs.vs_outputs(1)
    vs.call_fs()
    a = vs.alu()
    a.vec('MOV', 'R3', 'R1.xy01')
    vs.export('POS0', 'R3.xyzw', done=True)
    vs.export('PARAM0', 'R2.xy00', done=True)
    return vs


# bright: bloom threshold. PS UB0: [0] luminance weights, [1] (threshold, scale)
def bright():
    ps = Shader('ps', 'bright')
    ps.uniform_block(0, 'Bright', 256)
    ps.samplers(('uScene', 'sampler2D'))
    ps.ps_inputs(1)
    ps.tex().fetch('SAMPLE R1.xyzw, R0.xy00, t0, s0')
    a = ps.alu(kcache0=(0, 0, 15))
    a.dot4('R2.x', 'R1.xyz0', 'KC0[0].xyzw')
    a.group(('y', 'ADD', 'R2.y', 'R2.x', '-KC0[1].x'), ('z', 'ADD', 'R2.z', 'R2.x', '0.0001f'))
    a.group(('y', 'MAX', 'R2.y', 'R2.y', '0.0f'), ('t', 'RECIP_IEEE', 'R2.z', 'R2.z'))
    a.group(('y', 'MUL', 'R2.y', 'R2.y', 'R2.z'))
    a.group(('y', 'MUL', 'R2.y', 'R2.y', 'KC0[1].y'))
    a.vec('MUL', 'R3', 'R1.xyzw', 'R2.yyyy', mask='xyz', trans=('MOV', 'R3.w', '1.0f'))
    ps.export('PIX0', 'R3.xyzw', done=True)
    return fullscreen_vs('bright'), ps


# blur: separable 9-tap gaussian. PS UB0: [0] texel step, [1-2] weights 0-7, [3].x weight 8
def blur():
    ps = Shader('ps', 'blur')
    ps.uniform_block(0, 'Blur', 256)
    ps.samplers(('uSource', 'sampler2D'))
    ps.ps_inputs(1)
    a = ps.alu(kcache0=(0, 0, 15))
    for reg, (k0, k1) in zip(('R1', 'R2', 'R3', 'R4'), ((-4, -3), (-2, -1), (1, 2), (3, 4))):
        a.group(('x', 'MULADD', f'{reg}.x', 'KC0[0].x', f'{float(k0)}f', 'R0.x'),
                ('y', 'MULADD', f'{reg}.y', 'KC0[0].y', f'{float(k0)}f', 'R0.y'),
                ('z', 'MULADD', f'{reg}.z', 'KC0[0].x', f'{float(k1)}f', 'R0.x'),
                ('w', 'MULADD', f'{reg}.w', 'KC0[0].y', f'{float(k1)}f', 'R0.y'))
    t = ps.tex()
    taps = ['R1.xy00', 'R1.zw00', 'R2.xy00', 'R2.zw00', 'R0.xy00', 'R3.xy00', 'R3.zw00', 'R4.xy00', 'R4.zw00']
    for index, coord in enumerate(taps):
        t.fetch(f'SAMPLE R{5 + index}.xyzw, {coord}, t0, s0')
    a = ps.alu(kcache0=(0, 0, 15))
    weights = ['KC0[1].x', 'KC0[1].y', 'KC0[1].z', 'KC0[1].w', 'KC0[2].x', 'KC0[2].y', 'KC0[2].z', 'KC0[2].w', 'KC0[3].x']
    a.vec('MUL', 'R14', 'R5.xyzw', splat(weights[0]))
    for index in range(1, 9):
        a.vec('MULADD', 'R14', f'R{5 + index}.xyzw', splat(weights[index]), 'R14.xyzw')
    ps.export('PIX0', 'R14.xyzw', done=True)
    return fullscreen_vs('blur'), ps


# composite: scene + bloom, exposure, Reinhard tonemap, vignette.
# PS UB0: [0] (bloom intensity, exposure, vignette strength)
def composite():
    ps = Shader('ps', 'composite')
    ps.uniform_block(0, 'Composite', 256)
    ps.samplers(('uScene', 'sampler2D'), ('uBloom', 'sampler2D'))
    ps.ps_inputs(1)
    t = ps.tex()
    t.fetch('SAMPLE R1.xyzw, R0.xy00, t0, s0')
    t.fetch('SAMPLE R2.xyzw, R0.xy00, t1, s1')
    a = ps.alu(kcache0=(0, 0, 15))
    a.vec('MULADD', 'R3', 'R2.xyzw', 'KC0[0].xxxx', 'R1.xyzw', mask='xyz')
    a.vec('MUL', 'R3', 'R3.xyzw', 'KC0[0].yyyy', mask='xyz')
    a.vec('ADD', 'R4', 'R3.xyzw', '1.0f', mask='xyz')
    a.group(('x', 'ADD', 'R6.x', 'R0.x', '-0.5f'), ('y', 'ADD', 'R6.y', 'R0.y', '-0.5f'),
            ('t', 'RECIP_IEEE', 'R5.x', 'R4.x'))
    a.dot4('R6.z', 'R6.xy00', 'R6.xy00', trans=('RECIP_IEEE', 'R5.y', 'R4.y'))
    a.group(('w', 'MULADD', 'R6.w', '-R6.z', 'KC0[0].z', '1.0f', 'CLAMP'), ('t', 'RECIP_IEEE', 'R5.z', 'R4.z'))
    a.vec('MUL', 'R3', 'R3.xyzw', 'R5.xyzw', mask='xyz')
    a.vec('MUL', 'R3', 'R3.xyzw', 'R6.wwww', mask='xyz', trans=('MOV', 'R3.w', '1.0f'))
    ps.export('PIX0', 'R3.xyzw', done=True)
    return fullscreen_vs('composite'), ps


# particle: CPU-written vertices with RGBA8 colour. VS UB0: [0-3] view-projection rows.
def particle():
    vs = Shader('vs', 'particle')
    vs.uniform_block(0, 'Camera', 256)
    vs.attribs(('aPosition', 'vec3'), ('aColor', 'vec4'))
    vs.vs_outputs(1)
    vs.call_fs()
    a = vs.alu(kcache0=(0, 0, 15))
    mvp_rows(a, 'R3', 'R1', 'KC0', 0)
    vs.export('POS0', 'R3.xyzw', done=True)
    vs.export('PARAM0', 'R2.xyzw', done=True)

    ps = Shader('ps', 'particle')
    ps.ps_inputs(1)
    ps.export('PIX0', 'R0.xyzw', done=True)
    return vs, ps


# branch: nested if/else through the predicate stack (ALU_PUSH_BEFORE, JUMP,
# ELSE, ALU_POP_AFTER), the way compiled retail shaders branch.
#   cell = fract(uv * cells + scroll)
#   colour = cell.x > 0.5 ? (cell.y > 0.5 ? A : B) : C
# PS UB0: [0] (cells x, cells y, scroll x, scroll y), [1] A, [2] B, [3] C
def branch():
    ps = Shader('ps', 'branch')
    ps.uniform_block(0, 'Branch', 256)
    ps.ps_inputs(1)
    a = ps.alu(kcache0=(0, 0, 15))
    a.group(('x', 'MULADD', 'R1.x', 'R0.x', 'KC0[0].x', 'KC0[0].z'),
            ('y', 'MULADD', 'R1.y', 'R0.y', 'KC0[0].y', 'KC0[0].w'))
    a.group(('x', 'FRACT', 'R1.x', 'R1.x'), ('y', 'FRACT', 'R1.y', 'R1.y'))
    outer = ps.alu(kcache0=(0, 0, 15), kind='ALU_PUSH_BEFORE')
    outer.group(('x', 'PRED_SETGT', '____', 'R1.x', '0.5f', 'UPDATE_EXEC_MASK', 'UPDATE_PRED'))
    outer_jump = ps.cf_inst(None)  # patched below: jump to the outer ELSE
    inner = ps.alu(kcache0=(0, 0, 15), kind='ALU_PUSH_BEFORE')
    inner.group(('x', 'PRED_SETGT', '____', 'R1.y', '0.5f', 'UPDATE_EXEC_MASK', 'UPDATE_PRED'))
    inner_jump = ps.cf_inst(None)
    ps.alu(kcache0=(0, 0, 15)).vec('MOV', 'R2', 'KC0[1].xyzw')
    inner_else = ps.cf_inst(None)
    ps.alu(kcache0=(0, 0, 15), kind='ALU_POP_AFTER').vec('MOV', 'R2', 'KC0[2].xyzw')
    outer_else = ps.cf_inst(None)
    ps.alu(kcache0=(0, 0, 15), kind='ALU_POP_AFTER').vec('MOV', 'R2', 'KC0[3].xyzw')
    end = ps.next_cf()
    ps.cf[outer_jump] = f'JUMP ADDR({outer_else})'
    ps.cf[inner_jump] = f'JUMP ADDR({inner_else})'
    ps.cf[inner_else] = f'ELSE POP_CNT(1) ADDR({outer_else})'
    ps.cf[outer_else] = f'ELSE POP_CNT(1) ADDR({end})'
    ps.export('PIX0', 'R2.xyzw', done=True)
    return fullscreen_vs('branch'), ps


# loop: Julia-set iteration in a constant-count loop (LOOP_START/LOOP_END with
# loop constant i0 = 32 iterations), escape counted without breaking:
#   z = (uv - 0.5) * scale + centre;  repeat 32: z = z^2 + c; n += |z|^2 < 4
#   colour = mix(outside, inside, n / 32)
# PS UB0: [0] (c re, c im, scale, 1/32), [1] (centre x, centre y), [2] outside, [3] inside
LOOP_ITERATIONS = 32


def loop():
    ps = Shader('ps', 'loop')
    ps.uniform_block(0, 'Julia', 256)
    ps.ps_inputs(1)
    ps.set('LOOP_VARS[0].OFFSET', 0)
    ps.set('LOOP_VARS[0].VALUE', LOOP_ITERATIONS | (0 << 12) | (1 << 24))  # count | init | increment
    a = ps.alu(kcache0=(0, 0, 15))
    a.group(('x', 'ADD', 'R1.x', 'R0.x', '-0.5f'), ('y', 'ADD', 'R1.y', 'R0.y', '-0.5f'), ('z', 'MOV', 'R2.z', '0.0f'))
    a.group(('x', 'MULADD', 'R1.x', 'R1.x', 'KC0[0].z', 'KC0[1].x'),
            ('y', 'MULADD', 'R1.y', 'R1.y', 'KC0[0].z', 'KC0[1].y'))
    start = ps.cf_inst(None)
    body = ps.alu(kcache0=(0, 0, 15))
    # zr' = zr*zr - zi*zi + cr ; zi' = 2*zr*zi + ci
    body.group(('x', 'MUL', 'R3.x', 'R1.x', 'R1.x'), ('y', 'MUL', 'R3.y', 'R1.y', 'R1.y'),
               ('z', 'MUL', 'R3.z', 'R1.x', 'R1.y'))
    body.group(('x', 'ADD', 'R3.x', 'R3.x', '-R3.y'), ('z', 'ADD', 'R3.z', 'R3.z', 'R3.z'))
    body.group(('x', 'ADD', 'R1.x', 'R3.x', 'KC0[0].x'), ('y', 'ADD', 'R1.y', 'R3.z', 'KC0[0].y'))
    # Keep z finite: clamp to +-4 (an escaped point stays escaped).
    body.group(('x', 'MIN', 'R1.x', 'R1.x', '4.0f'), ('y', 'MIN', 'R1.y', 'R1.y', '4.0f'))
    body.group(('x', 'MAX', 'R1.x', 'R1.x', '-4.0f'), ('y', 'MAX', 'R1.y', 'R1.y', '-4.0f'))
    body.dot4('R3.w', 'R1.xy00', 'R1.xy00')
    body.group(('w', 'SETGT', 'R3.w', '4.0f', 'R3.w'))
    body.group(('z', 'ADD', 'R2.z', 'R2.z', 'R3.w'))
    end = ps.cf_inst(None)
    ps.cf[start] = f'LOOP_START CF_CONST(0) ADDR({end + 1})'
    ps.cf[end] = f'LOOP_END CF_CONST(0) ADDR({start + 1})'
    a = ps.alu(kcache0=(0, 0, 15))
    a.group(('z', 'MUL', 'R2.z', 'R2.z', 'KC0[0].w'))
    a.vec('ADD', 'R4', 'KC0[3].xyzw', '-KC0[2].xyzw')
    a.vec('MULADD', 'R5', 'R4.xyzw', 'R2.zzzz', 'KC0[2].xyzw')
    ps.export('PIX0', 'R5.xyzw', done=True)
    return fullscreen_vs('loop'), ps


# overlay: HUD text and swatches; pixel-space quads converted to NDC on the CPU.
def overlay():
    vs = Shader('vs', 'overlay')
    vs.attribs(('aPosition', 'vec2'), ('aTexCoord', 'vec2'), ('aColor', 'vec4'))
    vs.vs_outputs(2)
    vs.call_fs()
    a = vs.alu()
    a.vec('MOV', 'R4', 'R1.xy01')
    vs.export('POS0', 'R4.xyzw', done=True)
    vs.export('PARAM0', 'R2.xy00')
    vs.export('PARAM1', 'R3.xyzw', done=True)

    ps = Shader('ps', 'overlay')
    ps.samplers(('uFont', 'sampler2D'))
    ps.ps_inputs(2)  # R0 uv, R1 colour
    ps.tex().fetch('SAMPLE R2.xyzw, R0.xy00, t0, s0')
    a = ps.alu()
    a.vec('MUL', 'R3', 'R2.xyzw', 'R1.xyzw')
    ps.export('PIX0', 'R3.xyzw', done=True)
    return vs, ps


PROGRAMS = {'lit': lit, 'depth': depth, 'bright': bright, 'blur': blur, 'composite': composite, 'particle': particle,
            'overlay': overlay, 'lit_instanced': lit_instanced, 'branch': branch, 'loop': loop}


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for name, build in PROGRAMS.items():
        vs, ps = build()
        (out / f'{name}.vsh').write_text(vs.render())
        (out / f'{name}.psh').write_text(ps.render())
    print(' '.join(PROGRAMS))


if __name__ == '__main__':
    main()
