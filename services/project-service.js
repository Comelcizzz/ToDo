const ProjectModel = require('../models/project-model');
const ApiError = require('../exceptions/api-error');
const ProjectDto = require('../dtos/project-dto');

class ProjectService {
    // Method to create a new project
    async createProject({ projectTitle, description, user }) {
        try {
            // Create a new project instance
            const project = new ProjectModel({
                projectTitle, // Title of the project
                description,   // Description of the project
                user,          // User associated with the project
            });
            // Save the project to the database
            await project.save();
            // Return a Data Transfer Object (DTO) for the created project
            return new ProjectDto(project);
        } catch (error) {
            console.error('Error creating project:', error);
            throw new ApiError('Error creating project', 500); // Handle error if project creation fails
        }
    }
    
    // Method to fetch a project by ID
    async getProject(id) {
        if (!id) {
            throw ApiError.BadRequest("ID is required"); // Ensure ID is provided
        }
        console.log(`Fetching project with ID: ${id}`);
        try {
            const project = await ProjectModel.findById(id); // Find the project by ID
            if (!project) {
                console.log(`Project not found for ID: ${id}`);
                throw new ApiError.BadRequest("Project not found"); // Handle case when project is not found
            }
            console.log(`Project found: ${project}`);
            return project; // Return the found project
        } catch (error) {
            console.error(`Error fetching project: ${error.message}`);
            throw ApiError.InternalServerError("Error fetching project"); // Handle error when fetching project fails
        }
    }
    
    // Method to update an existing project
    async updateProject(userId, projectTitle, newTitle, newDescription, newCreatedAt) {
        try {
            console.log("Updating project for user:", userId);
            console.log("Current project title:", projectTitle);
    
            // Find the project by user ID and current title
            const project = await ProjectModel.findOne({
                user: userId, 
                projectTitle: projectTitle,
            });
    
            if (!project) {
                throw ApiError.BadRequest('Project not found'); // Handle case when project is not found
            }
    
            console.log("Found project:", project);
    
            // Update project details
            project.projectTitle = newTitle || project.projectTitle; // Update title if new title is provided
            project.description = newDescription || project.description; // Update description if new description is provided
    
            if (newCreatedAt) {
                project.createdAt = new Date(newCreatedAt); // Update createdAt if new date is provided
            }
    
            project.updatedAt = new Date(); // Update the updatedAt timestamp
    
            // Save the updated project to the database
            await project.save();
            console.log("Project updated successfully:", project);
            return new ProjectDto(project); // Return a DTO for the updated project
        } catch (e) {
            console.error('Error updating project:', e);
            throw ApiError.InternalServerError('Error updating project: ' + e.message); // Handle error when updating project fails
        }
    }
    
    // Method to delete a project by ID
    async deleteProject(id) {
        try {
            console.log(`Attempting to delete project with ID: ${id}`);
            const deletedProject = await ProjectModel.findByIdAndDelete(id); // Delete the project by ID
            if (!deletedProject) {
                console.log('No project found to delete');
                throw ApiError.NoProjectOnRequest(); // Handle case when no project is found to delete
            }
            console.log('Project deleted successfully:', deletedProject);
            return deletedProject._id; // Return the ID of the deleted project
        } catch (e) {
            console.error('Error in deleteProject:', e);
            throw ApiError.InternalServerError('Error deleting project'); // Handle error when deleting project fails
        }
    }
}

module.exports = new ProjectService();
