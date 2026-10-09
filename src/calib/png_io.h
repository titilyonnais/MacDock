// Lecture et écriture d'images PNG (WIC), en BGRA prémultiplié.
#pragma once
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace md {

bool writePng(const std::wstring& path, const std::uint8_t* bgra, UINT w, UINT h);
// Le même PNG, en mémoire. Vide si échec. Nécessite COM initialisé sur le thread.
// En mémoire, pixels en alpha droit (l'encodeur PNG de WIC ne prend pas le prémultiplié).
std::vector<std::uint8_t> encodePng(const std::uint8_t* bgra, UINT w, UINT h);
// Vide si échec. Nécessite COM initialisé sur le thread.
std::vector<std::uint8_t> readPng(const std::wstring& path, UINT& w, UINT& h);
// Redimensionnement (cubique de haute qualité) d'une image BGRA prémultipliée.
std::vector<std::uint8_t> resizeBgra(const std::vector<std::uint8_t>& src, UINT sw, UINT sh, UINT dw, UINT dh);

} // namespace md
