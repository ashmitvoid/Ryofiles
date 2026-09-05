#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

import os
import sys
from urllib.parse import unquote, urlparse

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

PORTAL_NAME = "org.freedesktop.portal.Desktop"
PORTAL_PATH = "/org/freedesktop/portal/desktop"
FILE_CHOOSER_IFACE = "org.freedesktop.portal.FileChooser"
REQUEST_IFACE = "org.freedesktop.portal.Request"


def fail(message: str) -> None:
    print(f"FileChooser broker matrix: {message}", file=sys.stderr)
    raise SystemExit(1)


def local_path(uri: str) -> str:
    parsed = urlparse(uri)
    if parsed.scheme != "file" or parsed.netloc not in ("", "localhost"):
        fail(f"expected a local file URI, got {uri!r}")
    return unquote(parsed.path)


def byte_array(value: str):
    return list(os.fsencode(value) + b"\0")


def unpack(value):
    if isinstance(value, GLib.Variant):
        return unpack(value.unpack())
    if isinstance(value, dict):
        return {key: unpack(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return type(value)(unpack(item) for item in value)
    return value


class PortalClient:
    def __init__(self):
        self.connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        unique_name = self.connection.get_unique_name()
        if not unique_name or not unique_name.startswith(":"):
            fail("session bus did not provide a unique caller name")
        self.sender = unique_name[1:].replace(".", "_")

    def call(
        self,
        method: str,
        token: str,
        title: str,
        options,
        expected_response: int = 0,
    ):
        handle = f"/org/freedesktop/portal/desktop/request/{self.sender}/{token}"
        loop = GLib.MainLoop()
        state = {"seen": False, "response": None, "results": None, "timeout": False}

        def on_response(_connection, _sender, object_path, _interface, _signal, parameters, _data):
            if object_path != handle:
                return
            response, results = parameters.unpack()
            state["seen"] = True
            state["response"] = response
            state["results"] = unpack(results)
            loop.quit()

        subscription = self.connection.signal_subscribe(
            PORTAL_NAME,
            REQUEST_IFACE,
            "Response",
            handle,
            None,
            Gio.DBusSignalFlags.NONE,
            on_response,
            None,
        )

        request_options = dict(options)
        request_options["handle_token"] = GLib.Variant("s", token)
        reply = self.connection.call_sync(
            PORTAL_NAME,
            PORTAL_PATH,
            FILE_CHOOSER_IFACE,
            method,
            GLib.Variant("(ssa{sv})", ("", title, request_options)),
            GLib.VariantType.new("(o)"),
            Gio.DBusCallFlags.NONE,
            8000,
            None,
        )
        returned_handle = reply.unpack()[0]
        if returned_handle != handle:
            self.connection.signal_unsubscribe(subscription)
            fail(f"{method} returned {returned_handle!r}; expected {handle!r}")

        def on_timeout():
            state["timeout"] = True
            loop.quit()
            return GLib.SOURCE_REMOVE

        timeout_id = GLib.timeout_add_seconds(8, on_timeout)
        loop.run()
        if not state["timeout"]:
            GLib.source_remove(timeout_id)
        self.connection.signal_unsubscribe(subscription)

        if state["timeout"] or not state["seen"]:
            fail(f"{method} timed out waiting for Request::Response")
        if state["response"] != expected_response:
            fail(
                f"{method} returned portal response {state['response']!r}; "
                f"expected {expected_response!r}"
            )
        return state["results"] or {}


def require_uris(results, expected, label):
    uris = list(unpack(results.get("uris", [])))
    actual_paths = [os.path.normpath(local_path(uri)) for uri in uris]
    expected_paths = [os.path.normpath(local_path(uri)) for uri in expected]
    if actual_paths != expected_paths:
        fail(
            f"{label} returned URIs {uris!r} (paths {actual_paths!r}); "
            f"expected {expected!r} (paths {expected_paths!r})"
        )


def main() -> int:
    if len(sys.argv) != 10:
        print(
            f"usage: {sys.argv[0]} OPEN_URI MULTI_A_URI MULTI_B_URI EDGE_URI FOLDER_URI "
            "SAVE_URI SAVEFILES_FOLDER_URI SAVEFILES_A_URI SAVEFILES_B_URI",
            file=sys.stderr,
        )
        return 2

    (
        open_uri,
        multi_a_uri,
        multi_b_uri,
        edge_uri,
        folder_uri,
        save_uri,
        savefiles_folder_uri,
        savefiles_a_uri,
        savefiles_b_uri,
    ) = sys.argv[1:]

    client = PortalClient()

    results = client.call(
        "OpenFile",
        "matrix_open",
        "Open broker matrix file",
        {"accept_label": GLib.Variant("s", "OPEN")},
    )
    require_uris(results, [open_uri], "OpenFile")

    results = client.call(
        "OpenFile",
        "matrix_multi_open",
        "Open multiple broker files",
        {"multiple": GLib.Variant("b", True)},
    )
    require_uris(results, [multi_a_uri, multi_b_uri], "multi OpenFile")

    results = client.call(
        "OpenFile",
        "matrix_edge_path",
        "Open edge filename",
        {},
    )
    require_uris(results, [edge_uri], "edge-path OpenFile")

    filters = [
        ("Images", [(0, "*.png")]),
        ("Text", [(0, "*.txt")]),
    ]
    choices = [
        ("readonly", "Open read-only", [], "false"),
        ("mode", "Mode", [("a", "Mode A"), ("b", "Mode B")], "a"),
    ]
    results = client.call(
        "OpenFile",
        "matrix_structured",
        "Open structured broker file",
        {
            "filters": GLib.Variant("a(sa(us))", filters),
            "current_filter": GLib.Variant("(sa(us))", filters[1]),
            "choices": GLib.Variant("a(ssa(ss)s)", choices),
        },
    )
    require_uris(results, [open_uri], "structured OpenFile")
    current_filter = unpack(results.get("current_filter"))
    if not current_filter or current_filter[0] != "Images":
        fail(f"structured OpenFile returned unexpected current_filter {current_filter!r}")
    returned_choices = dict(unpack(results.get("choices", [])))
    if returned_choices != {"readonly": "true", "mode": "b"}:
        fail(f"structured OpenFile returned unexpected choices {returned_choices!r}")

    results = client.call(
        "OpenFile",
        "matrix_folder",
        "Choose broker folder",
        {"directory": GLib.Variant("b", True)},
    )
    require_uris(results, [folder_uri], "folder OpenFile")

    save_parent = os.path.dirname(local_path(save_uri))
    results = client.call(
        "SaveFile",
        "matrix_save",
        "Save broker file",
        {
            "current_folder": GLib.Variant("ay", byte_array(save_parent)),
            "current_name": GLib.Variant("s", os.path.basename(local_path(save_uri))),
        },
    )
    require_uris(results, [save_uri], "SaveFile")

    savefiles_folder = local_path(savefiles_folder_uri)
    results = client.call(
        "SaveFiles",
        "matrix_save_files",
        "Save broker files",
        {
            "current_folder": GLib.Variant("ay", byte_array(savefiles_folder)),
            "files": GLib.Variant(
                "aay",
                [byte_array("alpha.txt"), byte_array("beta.txt")],
            ),
            "choices": GLib.Variant(
                "a(ssa(ss)s)",
                [("savefiles_marker", "Matrix marker", [], "true")],
            ),
        },
    )
    require_uris(results, [savefiles_a_uri, savefiles_b_uri], "SaveFiles")

    cancel_results = client.call(
        "OpenFile",
        "matrix_cancel",
        "Cancel broker request",
        {},
        expected_response=1,
    )
    cancel_uris = list(unpack(cancel_results.get("uris", [])))
    if cancel_uris:
        fail(f"cancelled OpenFile returned unexpected URIs {cancel_uris!r}")

    print("FileChooser public broker request matrix passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
