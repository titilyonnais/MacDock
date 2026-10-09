/* Version de MacDock, source unique (plan 52) : lue par le C++ (version.h), par les ressources VERSIONINFO des
   exécutables (res\version.rc2) et par la publication (l'étiquette vX.Y.Z doit lui correspondre, release.yml). Le
   mineur est le numéro du dernier plan fusionné. Macros seulement : rc.exe lit aussi ce fichier. */
#pragma once

#define MACDOCK_VERSION_MAJOR 0
#define MACDOCK_VERSION_MINOR 52
#define MACDOCK_VERSION_PATCH 0

#define MACDOCK_VERSION_STR2(x) #x
#define MACDOCK_VERSION_STR(x) MACDOCK_VERSION_STR2(x)
#define MACDOCK_VERSION_STRING                                                                                    \
    MACDOCK_VERSION_STR(MACDOCK_VERSION_MAJOR) "." MACDOCK_VERSION_STR(MACDOCK_VERSION_MINOR) "." MACDOCK_VERSION_STR(MACDOCK_VERSION_PATCH)
