FROM debian:bullseye

ARG USER_NAME=user

RUN apt-get update && apt-get install -y \
    bash sudo e2fsprogs && \
    apt-get clean

RUN echo "root:root" | chpasswd && \
    useradd -m -s /bin/bash ${USER_NAME} && \
    usermod -aG sudo ${USER_NAME} && \
    echo "${USER_NAME}:${USER_NAME}" | chpasswd && \
    echo "${USER_NAME} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

USER ${USER_NAME}
WORKDIR /home/${USER_NAME}
