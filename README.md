How to push changes to the GitHub

cd desktop/det

// det % echo -e ".DS_Store\n*.mp4\n*.mov" -- gets git to ignore video files. These need to be uploaded manually to the GDrive

git status //that tells you if you've made any changes in the working directory (i.e. your files currently don't match the repo)

git add . //adds all of the working changes (new files, deleted files)

git commit -am //a=all, m=message for commit ("add your changenotes in quotations")

git push //final push!!
