# Splits cpp-httplib into declarations (-v h=...) and implementation (-v cc=...),
# mirroring upstream split.py: lines between "// ----" border lines go to the
# .cc with "inline " removed.
BEGIN { impl = 0; print "#include \"httplib.h\"" > cc; print "namespace httplib {" > cc }
index($0, "// ----------------------------------------------------------------------------") { impl = !impl; next }
impl { gsub(/inline /, ""); print > cc; next }
{ print > h }
END { print "} // namespace httplib" > cc }
