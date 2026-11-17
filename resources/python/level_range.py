def level_range(text):
    def leading_int(part):
        digits = ""
        for ch in part:
            if ch.isdigit() or (ch == "-" and not digits):
                digits += ch
            else:
                break
        return int(digits) if digits not in ("", "-") else 0
    first, dots, last = str(text).partition("..")
    return (leading_int(first), leading_int(last)) if dots else (leading_int(first), leading_int(first))
