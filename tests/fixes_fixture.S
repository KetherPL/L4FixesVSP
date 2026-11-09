/* Synthetic i386 functions with the gamedata's original bytes and bounded sizes.
 * They are never executed. Hidden symbols exercise .symtab lookup, not dlsym. */
.text
.globl FixtureAnchor
.type FixtureAnchor, @function
FixtureAnchor:
    ret
.size FixtureAnchor, .-FixtureAnchor

.globl _ZN23CLagCompensationManager20StartLagCompensationEP11CBasePlayer19LagCompensationTypeRK6VectorRK6QAnglef
.hidden _ZN23CLagCompensationManager20StartLagCompensationEP11CBasePlayer19LagCompensationTypeRK6VectorRK6QAnglef
.type _ZN23CLagCompensationManager20StartLagCompensationEP11CBasePlayer19LagCompensationTypeRK6VectorRK6QAnglef, @function
_ZN23CLagCompensationManager20StartLagCompensationEP11CBasePlayer19LagCompensationTypeRK6VectorRK6QAnglef:
    .fill 0x305, 1, 0x90
    ret
    .fill 0x6CE - (. - _ZN23CLagCompensationManager20StartLagCompensationEP11CBasePlayer19LagCompensationTypeRK6VectorRK6QAnglef), 1, 0x90
    .byte 0xC7, 0x04, 0x24, 0x01, 0x02, 0x03, 0x04
    ret
.size _ZN23CLagCompensationManager20StartLagCompensationEP11CBasePlayer19LagCompensationTypeRK6VectorRK6QAnglef, .-_ZN23CLagCompensationManager20StartLagCompensationEP11CBasePlayer19LagCompensationTypeRK6VectorRK6QAnglef

.globl _ZN11CBaseClient17ProcessClientInfoEP14CLC_ClientInfo
.hidden _ZN11CBaseClient17ProcessClientInfoEP14CLC_ClientInfo
.type _ZN11CBaseClient17ProcessClientInfoEP14CLC_ClientInfo, @function
_ZN11CBaseClient17ProcessClientInfoEP14CLC_ClientInfo:
    .fill 23, 1, 0x90
    .byte 0x0F, 0xB6, 0x46, 0x54
    ret
.size _ZN11CBaseClient17ProcessClientInfoEP14CLC_ClientInfo, .-_ZN11CBaseClient17ProcessClientInfoEP14CLC_ClientInfo

.globl _ZN19CTerrorGameMovement15CheckForLaddersEb
.hidden _ZN19CTerrorGameMovement15CheckForLaddersEb
.type _ZN19CTerrorGameMovement15CheckForLaddersEb, @function
_ZN19CTerrorGameMovement15CheckForLaddersEb:
    .fill 0x355, 1, 0x90
    .byte 0x0F, 0x84, 0xC9, 0xFE, 0xFF, 0xFF
    ret
.size _ZN19CTerrorGameMovement15CheckForLaddersEb, .-_ZN19CTerrorGameMovement15CheckForLaddersEb
.section .note.GNU-stack,"",@progbits
