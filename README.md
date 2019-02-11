#mmf_writer_ros

Authors:

    Bruno Afonso <bafonso@gmail.com>

License:

    BSD

##Background

<https://github.com/samuellab/MMF-StaticBackgroundCompressor>

<https://github.com/samuellab/MMF-StaticBackgroundCompressor/blob/master/MMF%20Format%20description.pdf>

##Running

```shell
ROS_NAMESPACE=camera rosrun mmf_writer mmf_writer
```

```shell
ROS_NAMESPACE=camera roslaunch mmf_writer mmf_writer.launch manager:=camera_nodelet_manager
```

```shell
rosrun image_view image_view image:=/camera/blob_out/image_raw
```

```shell
rostopic echo /camera/blobs
```

```shell
rostopic pub -1 /camera/mmf_writer/save_background_image std_msgs/Empty
```

