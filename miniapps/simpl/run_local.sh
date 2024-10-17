# if [[ `git status --porcelain` ]]; then
  # echo "There are uncommited changes. Abort."
# else
  rm *.o
  make simpl -j
  make oc -j
  BACKTRACK="-ab -bb"
  for back in $BACKTRACK
  do
    for p in 21 22
    do
      for i in 6 7 8
      do
        mpirun -np 8 ./simpl -rs $i -rp 0 -p $p $back -vs 1
      done
    done
  done
  METHOD="oc mma"
  for method in $METHOD
  do
    mpirun -np 8 ./$method -rs 7 -rp 0 -p 22 -vs 1
  done
  # git add -f *.csv
  # git commit -m "Autogenerated commit from $(basename "$0")"
  # No changes
# fi
