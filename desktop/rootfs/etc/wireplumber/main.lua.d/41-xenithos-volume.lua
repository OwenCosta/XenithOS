-- XenithOS Desktop: how loud a sound device is the first time it's seen. WirePlumber's own start
-- is 40% on the slider, which sounds very quiet. This is 85% (the slider is cubic: 0.85 ^ 3).
-- (For WirePlumber 0.4, which Ubuntu 24.04 has. Newer ones read
-- /etc/wireplumber/wireplumber.conf.d/50-xenithos-volume.conf instead.)
if device_defaults and device_defaults.properties then
  device_defaults.properties["default-volume"] = 0.614
end
