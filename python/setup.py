from distutils.core import setup
from distutils.extension import Extension
from Cython.Build import cythonize

katherine_extension = Extension(
    name='katherine',
    sources=['katherine.pyx'],
    libraries=['katherine'],
    library_dirs=['../build/c'],
    include_dirs=['../c/include']
)
setup(
    name='katherine',
    ext_modules=cythonize([katherine_extension])
)
