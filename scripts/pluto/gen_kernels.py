#!/usr/bin/env python
import itertools
param_space = {'3d7pt':list(itertools.product([4,8,16,32], [8,16,32], [64, 128, 256, 512, 1024, 2048]))  }

def main():
  dry_run=0

  import os, subprocess, shutil
  from os.path import join as joinp
  from string import Template
  from scripts.utils import ensure_dir

  job_template=Template(
"""echo "$t1\\n$t1\\n$t2\\n$t3" > ${kernel_path}/tile.sizes && make -C ${kernel_path} -f ../../Makefile SRC=$kernel GEN_SRC=${kernel_name} DST=${kernel_name} """)

  # set the output path
  base_dir = 'pluto_examples/gen_kernels/'
  ensure_dir(base_dir)

  for kernel in ['3d7pt']:#, '3d25pt', '3d25pt_var', '3d7pt_var']:
#    print param_space[kernel]
    for p in param_space[kernel]:
      kernel_name="lbpar_" + kernel + "%d_%d_%d_%d"%(p[0], p[0], p[1], p[2])
      kernel_path = joinp(base_dir, kernel_name)

      job_cmd = job_template.safe_substitute(t1=p[0], t2=p[1], t3=p[2], kernel=kernel, kernel_name=kernel_name, kernel_path=kernel_path)
      ensure_dir(kernel_path)

      # copy the source file and append the tile size information
      c_tile_sizes = """
  tile_size = (int*) realloc((void *)tile_size, sizeof(int)*5);
  tile_size[0] = %d;
  tile_size[1] = %d;
  tile_size[2] = %d;
  tile_size[3] = %d;
  tile_size[4] = -1;
"""%(p[0], p[0], p[1], p[2])
      new_src_path = joinp(kernel_path, kernel+'.c') 
      orig_src_path = joinp('pluto_examples', kernel+'.c')
      shutil.copy(joinp('pluto_examples', 'print_utils.h'), joinp(kernel_path, 'print_utils.h'))
      with open(orig_src_path, 'r') as src_f, open(new_src_path, 'w') as dst_f:
        src_tmpl = Template(src_f.read())
        dst_f.write(src_tmpl.substitute(reset_tile_sizes=c_tile_sizes))
      print job_cmd
      if(dry_run==0): sts = subprocess.call(job_cmd, shell=True)


if __name__ == "__main__":
  main()