# Caution: Do not edit or save on Windows (or use Unix line endings \r)
echo "This scipt expects to be run on an unreliable server"
#
unreliableServer2 = "lenss-comp4.cse.tamu.edu"
reliableServer = "lenss-comp1.cse.tamu.edu"
#
# Start 3 worker processes
./fbsd -x $unreliableServer2 -r $reliableServer -l -w 10001 &
export pid1=$! # Get pid of last background process
./fbsd -x $unreliableServer2 -r $reliableServer -w 10002 -c 10001 &
export pid2=$! # Get pid of last background process
./fbsd -x $unreliableServer2 -r $reliableServer -w 10003 -c 10001 &
export pid3=$! # Get pid of last background process
echo "-------------------------------------------------------------------------"
echo "Press Enter to kill all processes started by this script..."
echo "Server PIDs:"
echo $pid1
echo $pid2
echo $pid3
echo "-------------------------------------------------------------------------"
read input
kill $pid1
kill $pid2
kill $pid3
