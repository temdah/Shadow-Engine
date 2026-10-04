"""Read flat RuntimeProfile initializers by declared field, not tail position."""
import re


def runtime_profile_field(source, profile_name, field):
    declaration = re.search(r'typedef struct RuntimeProfile\s*\{([^}]+)\}', source)
    assert declaration, 'RuntimeProfile declaration missing'
    body = re.sub(r'/\*.*?\*/', '', declaration[1], flags=re.S)
    body = re.sub(r'//[^\n]*', '', body)
    fields = []
    for statement in body.split(';'):
        for declarator in statement.strip().split(','):
            match = re.search(r'([A-Za-z_]\w*)\s*$', declarator)
            if match:
                fields.append(match[1])
    row = re.search(r'\{\s*(\d+)\s*,\s*"'+re.escape(profile_name)+r'"([^}]+)\}', source)
    assert row, profile_name
    values = [row[1], '"'+profile_name+'"'] + row[2].strip().lstrip(',').split(',')
    assert len(fields) == len(values), (profile_name, len(fields), len(values))
    return values[fields.index(field)].strip()
