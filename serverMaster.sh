# Caution: Do not edit or save on Windows (or use Unix line endings \r)
echo ""
echo "This scipt expects to be run on the reliable server"
echo "Starting 3 processes with the first as the leader"
echo ""
unreliableServer1="lenss-comp4.cse.tamu.edu"
unreliableServer2="lenss-comp4.cse.tamu.edu" #@TODO: Add another domain name when availible

./fbsd -x $unreliableServer1 -y $unreliableServer2 -m -l -w 10001 &
export pid1=$! # Get pid of last background process
./fbsd -x $unreliableServer1 -y $unreliableServer2 -m -w 10002 -c 10001 &
export pid2=$! # Get pid of last background process
./fbsd -x $unreliableServer1 -y $unreliableServer2 -m -w 10003 -c 10001 &
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
