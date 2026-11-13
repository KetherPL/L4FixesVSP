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

/* The short jump at 0x6F targets 0x90 (the function's return path in the engine). */
.globl _ZN13CSteam3Server34OnValidateAuthTicketResponseHelperEP11CBaseClient20EAuthSessionResponse
.hidden _ZN13CSteam3Server34OnValidateAuthTicketResponseHelperEP11CBaseClient20EAuthSessionResponse
.type _ZN13CSteam3Server34OnValidateAuthTicketResponseHelperEP11CBaseClient20EAuthSessionResponse, @function
_ZN13CSteam3Server34OnValidateAuthTicketResponseHelperEP11CBaseClient20EAuthSessionResponse:
    .fill 0x68, 1, 0x90
    .byte 0x83, 0xBF, 0x98, 0x00, 0x00, 0x00, 0x01, 0x74, 0x1F
    .fill 0x90 - (. - _ZN13CSteam3Server34OnValidateAuthTicketResponseHelperEP11CBaseClient20EAuthSessionResponse), 1, 0x90
    ret
.size _ZN13CSteam3Server34OnValidateAuthTicketResponseHelperEP11CBaseClient20EAuthSessionResponse, .-_ZN13CSteam3Server34OnValidateAuthTicketResponseHelperEP11CBaseClient20EAuthSessionResponse
.section .note.GNU-stack,"",@progbits
