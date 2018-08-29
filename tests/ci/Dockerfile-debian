FROM debian:buster

RUN echo "deb-src http://deb.debian.org/debian/ buster main" >> /etc/apt/sources.list
RUN apt-get update -qq
RUN apt-get install -yq build-essential
RUN apt-get build-dep -yq packagekit
RUN mkdir /build
WORKDIR /build
