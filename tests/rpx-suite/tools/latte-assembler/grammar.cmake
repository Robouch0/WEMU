file(READ ${IN} CONTENTS)
file(WRITE ${OUT} "const char *LatteGrammar = R\"(\n${CONTENTS})\";\n")
