/^# Packages using this file: / {
  s/# Packages using this file://
  ta
  :a
  s/ nagios-plugins / nagios-plugins /
  tb
  s/ $/ nagios-plugins /
  :b
  s/^/# Packages using this file:/
}
