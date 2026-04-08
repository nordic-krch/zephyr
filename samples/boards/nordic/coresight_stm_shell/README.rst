.. zephyr:code-sample:: coresight_stm_shell
   :name: Coresight STM with remote shell
   :relevant-api: log_api

Overview
********

This sample combines :zephyr:code-sample:`coresight_stm` (STM logging through Coresight on the
application core) with :ref:`shell remote <shell_remote>` on the Radio, PPR, and FLPR cores.

It is only supported on ``nrf54h20dk/nrf54h20/cpuapp``.

Requirements
************

* nRF54H20 DK
* Multi-image sysbuild (application core + remote images for the selected cores)

Building and running
**********************

.. code-block:: shell

   west build -b nrf54h20dk/nrf54h20/cpuapp samples/boards/nordic/coresight_stm_shell --sysbuild \
     -S nordic-log-stm -- \
     -DSB_CONFIG_APP_CPUPPR_RUN=y -DSB_CONFIG_APP_CPUFLPR_RUN=y

Flash all images (e.g. ``west flash --sysbuild``).

The ``nordic-log-stm`` snippet enables STM log frontend and the Coresight overlay on cpuapp.
Child images use the remote shell CLI over IPC; use the ``ping`` command from the host shell
(see :zephyr:code-sample:`shell_remote`).
