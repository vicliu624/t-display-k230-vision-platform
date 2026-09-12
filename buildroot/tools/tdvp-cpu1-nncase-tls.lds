/* SPDX-License-Identifier: MIT */
/* The pinned RT-Smart application script does not collect TLS. Without this
 * augmentation, orphan .tbss and .tbss.* sections can share a VMA and corrupt
 * caller/library thread-local state. Keep vendor entry/load/data rules intact.
 * Pass this augmentation BEFORE the vendor -T script. */
SECTIONS
{
    .tdata : ALIGN(8) { *(.tdata .tdata.*) }
    .tbss : ALIGN(8) { *(.tbss .tbss.*) *(.tcommon) }
}
INSERT BEFORE bss;
