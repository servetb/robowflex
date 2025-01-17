import xml.etree.ElementTree as ET
import shutil
import os
import sys

def translate_into_txt(filename):
    target_dir = os.getcwd() + '/envs/' + filename
    f_new = filename+'.txt'
    txt_file = []
    urdf = target_dir+'/urdf/'+filename+'.urdf'
    srdf = target_dir+'/srdf/'+filename+'.srdf'
    txt_file = parse_file(urdf,srdf)
    write_into_txt(txt_file,('obj_txt'+'/'+f_new))


def parse_file(urdf,srdf):
    #print(root_name)
    group_names = []

    root = ET.parse(srdf).getroot()
    for group in root.findall('group'):
        group_names.append(group.get('name'))

    #print(group_names)

    root = ET.parse(urdf).getroot()

    links = []
    for link in root.findall('link'):
        link_name = link.get('name')
        if (link.find('visual') != None and link.find('visual/geometry/mesh') == None and link.find('visual/geometry/box') != None
                and link.find('collision/geometry') != None):  # if object different type ?
            link_size = link.find('visual/geometry/box').get('size')
            link_xyz = link.find('visual/origin').get('xyz')
            link_rpy = link.find('visual/origin').get('rpy')
            line1 = link_name + ',' + link_size + ',' + link_rpy + ',' + link_xyz
            links.append(line1)
    #print(links)
    joints = []
    indices =[]

    for joint in root.findall('joint'):  # inside robowflex getACM (unnecessary)
        if(joint.get('type') == 'fixed'):
            continue
        j_name = joint.get('name')
        #print( j_name)
        if 1:
            origin_xyz = joint.find('origin').get('xyz')
            origin_rpy = joint.find('origin').get('rpy')
            j_name = joint.get('name')
            j_type = joint.get('type')
            j_axis = joint.find('axis').get('xyz')
            j_line = j_name + ','+ origin_xyz + ','+ origin_rpy + ','  + j_type + ',' + str(j_axis)
            joints.append(j_line)
            #print(j_line)

    merge = []
    for i in range(len(group_names)):
        line =  group_names[i]+','

        link = links[i]
        link_ = link[0:6]
        line += link
        for joint in joints:
            joint_ = joint[0:6]
            if(joint_ == link_):
                line += ',' + joint
                #print(line)

        line += ';'
        #print(line)
        merge.append(line)
    return merge


def parse_srdf_file(root):
    groups = []
    for group in root.findall('group'):
        groups_joint = group.get('name')
        joint_name = group.find('joint').get('name')
        g_line = joint_name + ',' + groups_joint
        groups.append(g_line)

    #print(groups)
    return groups
    # print(g_line)

def write_into_txt(file,name):

    with open(name, 'w') as f:
        for item in file:
            f.write("%s\n" % item)



if len(sys.argv) == 1:
     sys.exit("NOT ENOUGH ARGS")


os.chdir("../../..")
os.chdir(os.getcwd()+"/src/robowflex/robowflex_dart/include/io/")

translate_into_txt(str(sys.argv[1]))
# def main():
#     translate_into_txt("/envs")
# if __name__ == "__main__":
#     main()
