/* Minimal miniz_export.h stub to satisfy include when building tinyexr.
   Defines MINIZ_EXPORT as an empty macro so miniz headers compile in this
   project. The real miniz uses this macro for DLL export/import on Windows.
*/

#pragma once

#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

/* Export macro used by miniz. Define empty for static build. */
#ifndef MINIZ_EXPORT
#define MINIZ_EXPORT
#endif

#endif /* MINIZ_EXPORT_H */
