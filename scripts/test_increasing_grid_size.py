#!/usr/bin/env python
def igs_test(target_dir, exp_name, th, group='', params={}): 
  from scripts.conf.conf import machine_conf, machine_info
  from scripts.utils import run_test
  import itertools

  dry_run = 0
  is_dp=1

  cs = 4096

  # Test using rasonable time
  # T = scale * size / perf
  # scale = T*perf/size
  desired_time = 20
  if(machine_info['hostname']=='Haswell_18core'):
    k_perf_order = {0:1500, 1:5000, 4:400, 5:2000 ,6:100}
  else:
    k_perf_order = {0:1200, 1:3000, 4:350, 5:1500 ,6:80}
  k_time_scale={}
  for k, v in k_perf_order.items():
    k_time_scale[k] = desired_time*v

  points = list(range(32, 5000, 128))
  points = sorted(list(set(points)))

  if is_dp ==1:
    kernels_limits = [1057, 1200, 0, 0, 545, 680, 289]
  else:
    kernels_limits = [1350, 0, 0, 0, 801, 0, 0]

  if(machine_info['hostname']=='Haswell_18core'):
    if is_dp == 1:
      kernels_limits = [1600, 1600, 0, 0, 960, 1000, 500]
    else:
      kernels_limits = [2100, 0, 0, 0, 1200, 0, 0]

  count=0
  for ts, tgs_rl in [(2,[-1, 1]), (0,[0])]:
    for tgs_r in tgs_rl:
      for kernel, mwdt_list in [(0,[0,1]), (1,[0,2]), (4,[0,1]), (5,[1])]: #, 6]:
        if ts==0 or tgs_r==1:
          mwdt_list=[0]
        for mwdt in mwdt_list:
          for N in points:
            if (N < kernels_limits[kernel]):
              tb, nwf, tgs, thx, thy, thz = (-1,-1,tgs_r,tgs_r,tgs_r,tgs_r)
              key = (mwdt, kernel, N, tgs_r)
              if key in params.keys():
                tb, nwf, tgs, thx, thy, thz = params[key]
              outfile=('kernel%d_isdp%d_ts%d_mwdt%d_N%d_%s_%s.txt' % (kernel, is_dp, ts, mwdt, N, group, exp_name[-13:]))
              nt = max(int(k_time_scale[kernel]/(N**3/1e6)), 30)
              run_test(dry_run=dry_run, is_dp=is_dp, th=th, tgs=tgs, thx=thx, thy=thy, thz=thz, kernel=kernel, ts=ts, nx=N, ny=N, nz=N, nt=nt, outfile=outfile, target_dir=target_dir, cs=cs, mwdt=mwdt, tb=tb, nwf=nwf)
              count = count+1
  print "experiments count =" + str(count)


def main():
  from scripts.utils import create_project_tarball, get_stencil_num
  from scripts.conf.conf import machine_conf, machine_info
  import os
  from csv import DictReader
  import time,datetime

  sockets=1 # number of processors to use in the experiments

  time_stamp = datetime.datetime.fromtimestamp(time.time()).strftime('%Y%m%d_%H_%M')
  exp_name = "increasing_grid_size_sockets_%d_at_%s_%s" % (sockets,machine_info['hostname'], time_stamp)  

  tarball_dir='results/'+exp_name
  create_project_tarball(tarball_dir, "project_"+exp_name)
  target_dir='results/' + exp_name 

  # parse the results to obtain the selected parameters by the auto tuner
  data = []
  data_file = os.path.join('results', 'summary.csv')
  try:
    with open(data_file, 'rb') as output_file:
      raw_data = DictReader(output_file)
      for k in raw_data:
        k['stencil'] = get_stencil_num(k)
        k['method'] = 2 if 'Diamond' in k['Time stepper orig name'] else 0
        if k['method'] == 2:

          if k['Wavefront parallel strategy'] == 'Relaxed synchronization wavefront with fixed execution':
            k['mwdt'] = 3
          elif k['Wavefront parallel strategy'] == 'Relaxed synchronization wavefront':
            k['mwdt'] = 2
          elif k['Wavefront parallel strategy'] == 'Wavefront':
            k['mwdt'] = 0
          elif k['Wavefront parallel strategy'] == 'Fixed execution wavefronts':
            k['mwdt'] = 1
          if int(k['Thread group size']) == 1:
            k['mwdt'] = 0
        data.append(k)
  except:
     pass
  params = dict()
  for k in data:
    try:
      if k['method']==2:
        params[( k['mwdt'], k['stencil'], int(k['Global NX']), int(k['User set thread group size']) )] = (int(k['Time unroll']), int(k['Multi-wavefront updates']), int(k['Thread group size']), int(k['Threads along x-axis']), int(k['Threads along y-axis']), int(k['Threads along z-axis']))
    except:
      print k['User set thread group size'] , int(k['User set thread group size'])
      print k
      raise

  #update the pinning information to use all cores
  th = machine_info['n_cores']*sockets

  if sockets == 1:
    pin_str = "S1:0-%d "%(th-1)
  if sockets == 2:
    pin_str = "S0:0-%d@S1:0-%d -i "%(th/2-1, th/2-1)

  for group in ['MEM', 'DATA', 'TLB_DATA', 'L2', 'L3']:
    if(machine_info['hostname']=='Haswell_18core'):
      machine_conf['pinning_args'] = "-m -g " + group + " -C " + pin_str + '--'
    elif(machine_info['hostname']=='IVB_10core'):
      machine_conf['pinning_args'] = "-m -g MEM -C " + pin_str + '--'

#    for k,v in params.iteritems(): print k,v
    igs_test(target_dir, exp_name, th=th, params=params, group=group) 


if __name__ == "__main__":
  main()
