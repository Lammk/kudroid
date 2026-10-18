// Linux leaves upper argument bits unspecified; Apple requires caller extension.
#define GL_MATRIX_ENTRY(name) \
    GL_BOOLEAN_ENTRY(name, (int location, int count, unsigned transpose, const float* value), \
                     (int, int, unsigned char, const float*), \
                     (location, count, static_cast<unsigned char>(transpose), value))
GL_MATRIX_ENTRY(glUniformMatrix2x3fv)
GL_MATRIX_ENTRY(glUniformMatrix3x2fv)
GL_MATRIX_ENTRY(glUniformMatrix2x4fv)
GL_MATRIX_ENTRY(glUniformMatrix4x2fv)
GL_MATRIX_ENTRY(glUniformMatrix3x4fv)
GL_MATRIX_ENTRY(glUniformMatrix4x3fv)
#undef GL_MATRIX_ENTRY

#define GL_PROGRAM_MATRIX_ENTRY(name) \
    GL_BOOLEAN_ENTRY(name, (unsigned program, int location, int count, unsigned transpose, const float* value), \
                     (unsigned, int, int, unsigned char, const float*), \
                     (program, location, count, static_cast<unsigned char>(transpose), value))
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix2fv)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix3fv)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix4fv)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix2x3fv)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix3x2fv)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix2x4fv)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix4x2fv)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix3x4fv)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix4x3fv)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix2fvEXT)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix3fvEXT)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix4fvEXT)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix2x3fvEXT)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix3x2fvEXT)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix2x4fvEXT)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix4x2fvEXT)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix3x4fvEXT)
GL_PROGRAM_MATRIX_ENTRY(glProgramUniformMatrix4x3fvEXT)
#undef GL_PROGRAM_MATRIX_ENTRY

#define GL_COLOR_MASK_ENTRY(name) \
    GL_BOOLEAN_ENTRY(name, (unsigned index, unsigned r, unsigned g, unsigned b, unsigned a), \
                     (unsigned, unsigned char, unsigned char, unsigned char, unsigned char), \
                     (index, static_cast<unsigned char>(r), static_cast<unsigned char>(g), \
                      static_cast<unsigned char>(b), static_cast<unsigned char>(a)))
GL_COLOR_MASK_ENTRY(glColorMaski)
GL_COLOR_MASK_ENTRY(glColorMaskiOES)
GL_COLOR_MASK_ENTRY(glColorMaskiEXT)
#undef GL_COLOR_MASK_ENTRY

GL_BOOLEAN_ENTRY(glSampleCoverage, (float value, unsigned invert), (float, unsigned char),
                 (value, static_cast<unsigned char>(invert)))
GL_BOOLEAN_ENTRY(glVertexAttribFormat,
                 (unsigned index, int size, unsigned type, unsigned normalized, unsigned offset),
                 (unsigned, int, unsigned, unsigned char, unsigned),
                 (index, size, type, static_cast<unsigned char>(normalized), offset))
GL_BOOLEAN_ENTRY(glTexStorage2DMultisample,
                 (unsigned target, int samples, unsigned format, int width, int height, unsigned fixed),
                 (unsigned, int, unsigned, int, int, unsigned char),
                 (target, samples, format, width, height, static_cast<unsigned char>(fixed)))
#define GL_STORAGE_3D_ENTRY(name) \
    GL_BOOLEAN_ENTRY(name, \
                     (unsigned target, int samples, unsigned format, int width, int height, int depth, unsigned fixed), \
                     (unsigned, int, unsigned, int, int, int, unsigned char), \
                     (target, samples, format, width, height, depth, static_cast<unsigned char>(fixed)))
GL_STORAGE_3D_ENTRY(glTexStorage3DMultisample)
GL_STORAGE_3D_ENTRY(glTexStorage3DMultisampleOES)
#undef GL_STORAGE_3D_ENTRY

#define GL_DEBUG_CONTROL_ENTRY(name) \
    GL_BOOLEAN_ENTRY(name, (unsigned source, unsigned type, unsigned severity, int count, const unsigned* ids, unsigned enabled), \
                     (unsigned, unsigned, unsigned, int, const unsigned*, unsigned char), \
                     (source, type, severity, count, ids, static_cast<unsigned char>(enabled)))
GL_DEBUG_CONTROL_ENTRY(glDebugMessageControl)
GL_DEBUG_CONTROL_ENTRY(glDebugMessageControlKHR)
#undef GL_DEBUG_CONTROL_ENTRY
