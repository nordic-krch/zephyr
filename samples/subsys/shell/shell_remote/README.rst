.. zephyr:code-sample:: shell-remote
   :name: Remote shell over IPC
   :relevant-api: shell_api

   Forward shell commands to another core using ``remote_shell``.

Overview
********

This sample uses Sysbuild to run two Zephyr images: a **host** (UART shell) and a **remote**
core with no serial console. The IPC service carries shell I/O between them, as described in
:zephyr:code-sample:`ipc-icmsg` for the underlying transport.

On the host UART shell, use::

   remote_shell <remote_core_name> <command> [args...]

to run ``<command>`` on the remote image. The valid ``<remote_core_name>`` values are the
connection names registered for your SoC (for example ``net`` on nRF5340 when the network
core is the remote peer). Use Tab completion on ``remote_shell`` to list subcommands on your
build.

The sample registers a ``ping`` command on **both** images: run ``ping`` on the host for a
local response, and ``remote_shell <remote_core_name> ping`` to run the same command on the
remote core (each prints ``pong`` with that image's ``CONFIG_BOARD_TARGET``).

Requirements
************

* A board supported by this sample (see ``sample.yaml``), built with Sysbuild and an
  appropriate ``REMOTE_BOARD`` for the remote image.

Building and running
********************

Build sample for a supported target with Sysbuild.

Connect a serial terminal to the **host** UART (115200 8N1), reset the board, then try::

   uart:~$ ping
   uart:~$ remote_shell net kernel version

(Replace ``net`` with the subcommand name your platform provides.)
