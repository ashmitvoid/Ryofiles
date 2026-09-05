#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

import sys

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402


PORTAL_NAME = "org.freedesktop.portal.Desktop"
PORTAL_PATH = "/org/freedesktop/portal/desktop"
FILE_CHOOSER_IFACE = "org.freedesktop.portal.FileChooser"
REQUEST_IFACE = "org.freedesktop.portal.Request"
TOKEN = "ryofiles_broker_smoke"


def fail(message: str) -> None:
    print(f"FileChooser broker smoke: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} EXPECTED_FILE_URI", file=sys.stderr)
        return 2

    expected_uri = sys.argv[1]
    connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    unique_name = connection.get_unique_name()
    if not unique_name or not unique_name.startswith(":"):
        fail("session bus did not provide a unique caller name")

    sender = unique_name[1:].replace(".", "_")
    expected_handle = (
        f"/org/freedesktop/portal/desktop/request/{sender}/{TOKEN}"
    )

    loop = GLib.MainLoop()
    state = {"seen": False, "response": None, "results": None, "timed_out": False}

    def on_response(_connection, _sender, object_path, _interface, _signal, parameters, _data):
        if object_path != expected_handle:
            return
        response, results = parameters.unpack()
        state["seen"] = True
        state["response"] = response
        state["results"] = results
        loop.quit()

    subscription = connection.signal_subscribe(
        PORTAL_NAME,
        REQUEST_IFACE,
        "Response",
        expected_handle,
        None,
        Gio.DBusSignalFlags.NONE,
        on_response,
        None,
    )

    options = {
        "handle_token": GLib.Variant("s", TOKEN),
        "accept_label": GLib.Variant("s", "OPEN BROKER SMOKE"),
        "modal": GLib.Variant("b", False),
    }
    reply = connection.call_sync(
        PORTAL_NAME,
        PORTAL_PATH,
        FILE_CHOOSER_IFACE,
        "OpenFile",
        GLib.Variant("(ssa{sv})", ("", "Broker smoke file", options)),
        GLib.VariantType.new("(o)"),
        Gio.DBusCallFlags.NONE,
        8000,
        None,
    )
    returned_handle = reply.unpack()[0]
    if returned_handle != expected_handle:
        connection.signal_unsubscribe(subscription)
        fail(
            f"unexpected Request handle {returned_handle!r}; "
            f"expected {expected_handle!r}"
        )

    def on_timeout():
        state["timed_out"] = True
        loop.quit()
        return GLib.SOURCE_REMOVE

    timeout_id = GLib.timeout_add_seconds(8, on_timeout)
    loop.run()
    if not state["timed_out"]:
        GLib.source_remove(timeout_id)
    connection.signal_unsubscribe(subscription)

    if state["timed_out"] or not state["seen"]:
        fail("timed out waiting for Request::Response")
    if state["response"] != 0:
        fail(f"portal returned response {state['response']!r}")

    results = state["results"] or {}
    uris = results.get("uris", [])
    if expected_uri not in uris:
        fail(f"expected URI {expected_uri!r} not found in {uris!r}")

    print("FileChooser public broker client smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
