# @license   http://www.gnu.org/licenses/gpl.html GPL Version 3
# @author    OpenMediaVault Plugin Developers <plugins@omv-extras.org>
# @copyright Copyright (c) 2025-2026 openmediavault plugin developers
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

{% set _raw = salt['omv_conf.get']('conf.service.snapraidaio.config') %}
{% set configs = _raw if _raw is iterable and _raw is not mapping else ([_raw] if _raw else []) %}

# Ensure at least one state runs so Salt exits 0 when the config list is empty.
snapraidaio_noop:
  test.noop: []

{% for conf in configs %}

configure_snapraidaio_{{ conf.uuid }}_conf:
  file.managed:
    - name: /etc/snapraid-aio-{{ conf.uuid }}.conf
    - source:
      - salt://{{ tpldir }}/files/etc-snapraid-aio_conf.j2
    - template: jinja
    - context:
        config: {{ conf | json }}
    - user: root
    - group: root
    - mode: '0640'

{% endfor %}
