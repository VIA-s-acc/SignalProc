# import: import io
# import: import zipfile
def _export_report_file_json(s):
    out = '"'
    for ch in s:
        out += "\\" + ch if ch in '"\\' else (f"\\u{ord(ch):04x}" if ord(ch) < 0x20 else ch)
    return out + '"'


def _export_report_file_csv(s):
    return '"' + s.replace('"', '""') + '"' if any(c in s for c in ',"\r\n') else s


def _export_report_file_number(v):
    return f"{v:.17g}" if np.isfinite(v) else "null"


def export_report_file(path, fields, passed, fmt):
    path = Path(path)
    if fmt == "npz":
        members = [(k, np.array(v)) for k, v in fields.items()] + [("passed", np.array(1.0 if passed else 0.0)), ("fields", np.array(list(fields), dtype=str))]
        with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as z:
            for key, array in members:
                buffer = io.BytesIO()
                np.lib.format.write_array(buffer, array, version=(1, 0))
                z.writestr(zipfile.ZipInfo(key + ".npy"), buffer.getvalue())
        return {"path": str(path), "bytes": float(path.stat().st_size)}
    if fmt == "csv":
        text = "field,value\n" + "".join(f"{k}," + (_export_report_file_csv(v) if isinstance(v, str) else _export_report_file_number(v)) + "\n" for k, v in fields.items())
    else:
        text = '{"passed":' + ("true" if passed else "false") + ',"fields":{' + ",".join(
            _export_report_file_json(k) + ":" + (_export_report_file_json(v) if isinstance(v, str) else _export_report_file_number(v)) for k, v in fields.items()) + "}}\n"
    data = text.encode()
    path.write_bytes(data)
    return {"path": str(path), "bytes": float(len(data))}
