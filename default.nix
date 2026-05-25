{ stdenv
, cmake
, ninja
, pkg-config
, zstd
, goodnet-core
, lib
}:

stdenv.mkDerivation {
  pname   = "goodnet-handler-zstd-decompress";
  version = "0.1.0";
  src     = ./.;
  nativeBuildInputs = [ cmake ninja pkg-config ];
  buildInputs       = [ goodnet-core zstd ];
  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DBUILD_TESTING=OFF"
  ];
  doCheck = false;

  meta = {
    description = "GoodNet plugin: zstd decompression middleware handler.";
    license     = lib.licenses.mit;
    platforms   = lib.platforms.linux;
  };
}
