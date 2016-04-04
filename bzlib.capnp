@0x8484d2d77983b934;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("bz2");

interface Bz2 {
  compressStream @0 (ifd :Int32,
                     ofd :Int32,
                     blockSize100k :Int32,
                     verbosity :Int32,
                     workFactor :Int32) -> (result :Int32);
  decompressStream @1 (ifd :Int32,
                       ofd :Int32,
                       verbosity :Int32,
                       small :Int32) -> (result :Int32);
  testStream @2 (ifd :Int32,
                 verbosity :Int32,
                 small :Int32) -> (result :Int32);
  libVersion @3 () -> (version :Text);
}
