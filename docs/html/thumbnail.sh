# rethumbnail the images
cd img
images="gpk-*.png kpk-*.png pk-*.png assassin.png list*.png"
for image in $images; do
	echo "thumbnailing $image"
	convert -geometry 300x170 $image thumbnails/$image
done
cd -

