# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_BARE_METAL
  reset.S
)

zephyr_library_sources_ifndef(CONFIG_BARE_METAL
  vector_table.S
  reset.S
  fault.c
  scb.c
  irq_init.c
  thread_abort.c
  )

zephyr_linker_sources_ifdef(CONFIG_SW_VECTOR_RELAY
  ROM_START
  SORT_KEY 0x0relay_vectors
  relay_vector_table.ld
  )

zephyr_linker_sources_ifdef(CONFIG_SW_VECTOR_RELAY
  RAM_SECTIONS
  vt_pointer_section.ld
  )
