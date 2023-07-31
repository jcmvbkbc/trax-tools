#!/bin/bash -e

[ -f trax-trace.conf ] && . trax-trace.conf

while : ; do
	case "$1" in
		-c)
			conf="$2"
			. "$conf"
			shift 2
			;;
		--kernel-start)
			kernel_start="$2"
			shift 2
			;;
		--kernel-end)
			kernel_end="$2"
			shift 2
			;;
		--kernel)
			kernel="$2"
			shift 2
			;;
		--rootfs-start)
			rootfs_start="$2"
			shift 2
			;;
		--rootfs-end)
			rootfs_end="$2"
			shift 2
			;;
		--rootfs)
			rootfs="$2"
			shift 2
			;;
		--rootfs-base-dir)
			rootfs_base_dir="$2"
			shift 2
			;;
		--trax-log)
			trax_log="$2"
			shift 2
			;;
		--cramfsck)
			CRAMFSCK="$2"
			shift 2
			;;
		--objdump)
			OBJDUMP="$2"
			shift 2
			;;
		*)
			break
			;;
	esac
done

function die()
{
	echo "$1"
	exit 1
}

[ -n "$kernel_start" ]    || die "--kernel-start is missing"
[ -n "$kernel_end" ]      || die "--kernel-end is missing"
[ -f "$kernel" ]          || die "--kernel is missing or is not a file"
[ -n "$rootfs_start" ]    || die "--rootfs-start is missing"
[ -n "$rootfs_end" ]      || die "--rootfs-end is missing"
[ -f "$rootfs" ]          || die "--rootfs is missing or is not a file"
[ -d "$rootfs_base_dir" ] || die "--rootfs-base-dir is missing or is not a directory"
[ -x "$OBJDUMP" ]         || die "--objdump is missing or is not executable"
[ -x "$CRAMFSCK" ]        || die "--cramfsck is missing or is not executable"

#
# $1 -- start address
# $2 -- end address
#
function decode_range()
{
	if test '(' $(($1)) -ge $(($kernel_start)) ')' -a '(' $(($1)) -lt $(($kernel_end)) ')' ; then
		echo "kernel"
		$OBJDUMP ${OBJDUMP_OPTIONS:--d} --start-address=$1 --stop-address=$(($2 + 3)) "$kernel" | sed -n '/^ *[0-9a-f]\+[: ]/p'
	elif test '(' $(($1)) -ge $(($rootfs_start)) ')' -a '(' $(($1)) -lt $(($rootfs_end)) ')' ; then
		start_tr=`$CRAMFSCK -t $(( $1 - $rootfs_start )) "$rootfs" | head -n1`
		start=`echo "$start_tr" | sed 's/.* offset //'`
		end_tr=`$CRAMFSCK -t $(( $2 - $rootfs_start )) "$rootfs" | head -n1`
		end=`echo "$end_tr" | sed 's/.* offset //'`
		file=`echo "$start_tr" | sed "s/.* file '\([^']*\)' offset .*/\1/"`
		echo "rootfs $file $1 - $2 ($start - $end)"
		$OBJDUMP ${OBJDUMP_OPTIONS:--d} --start-address=$start --stop-address=$(($end + 3)) "$rootfs_base_dir$file" | sed -n '/^ *[0-9a-f]\+[: ]/p'
	else
		echo '????'
	fi
}

rep=0
grep -- ' -> ' "${trax_log:--}" | while read trax_line ; do
	from=`echo "$trax_line" | sed 's/.* \(0x[0-9a-f]*\) -> .*/\1/'`
	to=`echo "$trax_line" | sed 's/.* -> \(0x[0-9a-f]*\) .*/\1/'`
	if [ -n "$start" ] ; then
		if [ "$prev_start" = "$start" -a "$prev_from" = "$from" ] ; then
			rep=$(( $rep + 1 ))
		else
			test "$rep" -gt 1 && echo " * $(( $rep + 1 )) times"
			rep=0
			prev_start=$start
			prev_from=$from
			decode_range $start $from
		fi
	fi
	echo "$from ->  $to"
	start=$to
done
